#include <assert.h>
#include <extension/extension.h>
#include <dlib/dstrings.h>
#include <dlib/log.h>
#include <dlib/json.h>
#include <emscripten/emscripten.h>
#include <script/script.h>
#include <unistd.h>
#include <stdlib.h>

#define LIB_NAME "facebook"

// Must match iOS for now
enum State
{
    STATE_CREATED              = 0,
    STATE_CREATED_TOKEN_LOADED = 1,
    STATE_CREATED_OPENING      = 2,
    STATE_OPEN                 = 1 | (1 << 9),
    STATE_OPEN_TOKEN_EXTENDED  = 2 | (1 << 9),
    STATE_CLOSED_LOGIN_FAILED  = 1 | (1 << 8),
    STATE_CLOSED               = 2 | (1 << 8)
};

// Must match iOS for now
enum Audience
{
    AUDIENCE_NONE = 0,
    AUDIENCE_ONLYME = 10,
    AUDIENCE_FRIENDS = 20,
    AUDIENCE_EVERYONE = 30
};

struct Facebook
{
    Facebook()
    {
        memset(this, 0, sizeof(*this));
        m_Callback = LUA_NOREF;
        m_Self = LUA_NOREF;
        m_Initialized = false;
    }
    int m_Callback;
    int m_Self;
    const char* m_appId;
    bool m_Initialized;
};

Facebook g_Facebook;

static void PushError(lua_State*L, const char* error)
{
    // Could be extended with error codes etc
    if (error != NULL) {
        lua_newtable(L);
        lua_pushstring(L, "error");
        lua_pushstring(L, error);
        lua_rawset(L, -3);
    } else {
        lua_pushnil(L);
    }
}

static void RunStateCallback(lua_State* L, int state, const char *error)
{
    if (g_Facebook.m_Callback != LUA_NOREF) {
        int top = lua_gettop(L);

        int callback = g_Facebook.m_Callback;
        g_Facebook.m_Callback = LUA_NOREF;
        lua_rawgeti(L, LUA_REGISTRYINDEX, callback);

        // Setup self
        lua_rawgeti(L, LUA_REGISTRYINDEX, g_Facebook.m_Self);
        lua_pushvalue(L, -1);
        dmScript::SetInstance(L);

        if (!dmScript::IsInstanceValid(L))
        {
            dmLogError("Could not run facebook callback because the instance has been deleted.");
            lua_pop(L, 2);
            assert(top == lua_gettop(L));
            return;
        }

        lua_pushnumber(L, (lua_Number) state);
        PushError(L, error);

        int ret = lua_pcall(L, 3, LUA_MULTRET, 0);
        if (ret != 0) {
            dmLogError("Error running facebook callback: %s", lua_tostring(L,-1));
            lua_pop(L, 1);
        }
        assert(top == lua_gettop(L));
        luaL_unref(L, LUA_REGISTRYINDEX, callback);
    } else {
        dmLogError("No callback set");
    }
}

static void RunCallback(lua_State* L, const char *error)
{
    if (g_Facebook.m_Callback != LUA_NOREF) {
        int top = lua_gettop(L);

        int callback = g_Facebook.m_Callback;
        lua_rawgeti(L, LUA_REGISTRYINDEX, callback);

        // Setup self
        lua_rawgeti(L, LUA_REGISTRYINDEX, g_Facebook.m_Self);
        lua_pushvalue(L, -1);
        dmScript::SetInstance(L);

        if (!dmScript::IsInstanceValid(L))
        {
            dmLogError("Could not run facebook callback because the instance has been deleted.");
            lua_pop(L, 2);
            assert(top == lua_gettop(L));
            return;
        }

        PushError(L, error);

        int ret = lua_pcall(L, 2, LUA_MULTRET, 0);
        if (ret != 0) {
            dmLogError("Error running facebook callback: %s", lua_tostring(L,-1));
            lua_pop(L, 1);
        }
        assert(top == lua_gettop(L));
        luaL_unref(L, LUA_REGISTRYINDEX, callback);
    } else {
        dmLogError("No callback set");
    }
}

static void RunDialogResultCallback(lua_State* L, const char *url, const char *error)
{
    if (g_Facebook.m_Callback != LUA_NOREF) {
        int top = lua_gettop(L);

        int callback = g_Facebook.m_Callback;
        lua_rawgeti(L, LUA_REGISTRYINDEX, callback);

        // Setup self
        lua_rawgeti(L, LUA_REGISTRYINDEX, g_Facebook.m_Self);
        lua_pushvalue(L, -1);
        dmScript::SetInstance(L);

        if (!dmScript::IsInstanceValid(L))
        {
            dmLogError("Could not run facebook callback because the instance has been deleted.");
            lua_pop(L, 2);
            assert(top == lua_gettop(L));
            return;
        }

        lua_createtable(L, 0, 1);
        lua_pushliteral(L, "url");
        // TODO: handle url=0 ?
        if (url) {
            lua_pushstring(L, url);
        } else {
            lua_pushnil(L);
        }
        lua_rawset(L, -3);

        PushError(L, error);

        int ret = lua_pcall(L, 3, LUA_MULTRET, 0);
        if (ret != 0) {
            dmLogError("Error running facebook callback: %s", lua_tostring(L,-1));
            lua_pop(L, 1);
        }
        assert(top == lua_gettop(L));
        luaL_unref(L, LUA_REGISTRYINDEX, callback);
    } else {
        dmLogError("No callback set");
    }
}

static void VerifyCallback(lua_State* L)
{
    if (g_Facebook.m_Callback != LUA_NOREF) {
        dmLogError("Unexpected callback set");
        luaL_unref(L, LUA_REGISTRYINDEX, g_Facebook.m_Callback);
        luaL_unref(L, LUA_REGISTRYINDEX, g_Facebook.m_Self);
        g_Facebook.m_Callback = LUA_NOREF;
        g_Facebook.m_Self = LUA_NOREF;
    }
}

// TODO: Move out to common stuff (also in engine/iap/src/iap_android.cpp and script_json among others)
static int ToLua(lua_State*L, dmJson::Document* doc, int index)
{
    const dmJson::Node& n = doc->m_Nodes[index];
    const char* json = doc->m_Json;
    int l = n.m_End - n.m_Start;
    switch (n.m_Type)
    {
    case dmJson::TYPE_PRIMITIVE:
        if (l == 4 && memcmp(json + n.m_Start, "null", 4) == 0) {
            lua_pushnil(L);
        } else if (l == 4 && memcmp(json + n.m_Start, "true", 4) == 0) {
            lua_pushboolean(L, 1);
        } else if (l == 5 && memcmp(json + n.m_Start, "false", 5) == 0) {
            lua_pushboolean(L, 0);
        } else {
            double val = atof(json + n.m_Start);
            lua_pushnumber(L, val);
        }
        return index + 1;

    case dmJson::TYPE_STRING:
        lua_pushlstring(L, json + n.m_Start, l);
        return index + 1;

    case dmJson::TYPE_ARRAY:
        lua_createtable(L, n.m_Size, 0);
        ++index;
        for (int i = 0; i < n.m_Size; ++i) {
            index = ToLua(L, doc, index);
            lua_rawseti(L, -2, i+1);
        }
        return index;

    case dmJson::TYPE_OBJECT:
        lua_createtable(L, 0, n.m_Size);
        ++index;
        for (int i = 0; i < n.m_Size; i += 2) {
            index = ToLua(L, doc, index);
            index = ToLua(L, doc, index);
            lua_rawset(L, -3);
        }

        return index;
    }

    assert(false && "not reached");
    return index;
}


typedef void (*OnLoginCallback)(void *L, int state, const char* error);

void OnLoginComplete(void* L, int state, const char* error)
{
    dmLogDebug("FB login complete...(%d, %s)", state, error);
    RunStateCallback((lua_State*)L, state, error);
}

int Facebook_Login(lua_State* L)
{
    int top = lua_gettop(L);
    VerifyCallback(L);

    dmLogDebug("Logging in to FB...");
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    g_Facebook.m_Callback = luaL_ref(L, LUA_REGISTRYINDEX);

    dmScript::GetInstance(L);
    g_Facebook.m_Self = luaL_ref(L, LUA_REGISTRYINDEX);

    // https://developers.facebook.com/docs/reference/javascript/FB.login/v2.0
    EM_ASM_ARGS({
        var state_open    = $0;
        var state_closed  = $1;
        var state_failed  = $2;
        var callback      = $3;
        var lua_state     = $4;

        FB.login(function(response) {
            var e = (response && response.error ? response.error.message : 0);
            if (e == 0 && response.authResponse) {
                Runtime.dynCall('viii', callback, [lua_state, state_open, 0]);
            } else if (e != 0) {
                var buf = allocate(intArrayFromString(e), 'i8', ALLOC_STACK);
                Runtime.dynCall('viii', callback, [lua_state, state_closed, buf]);
            } else {
                // No authResponse. Below text is from facebook's own example of this case.
                e = 'User cancelled login or did not fully authorize.';
                var buf = allocate(intArrayFromString(e), 'i8', ALLOC_STACK);
                Runtime.dynCall('viii', callback, [lua_state, state_failed, buf]);
            }
        }, {scope: 'public_profile,user_friends'});
    }, STATE_OPEN, STATE_CLOSED, STATE_CLOSED_LOGIN_FAILED, (OnLoginCallback) OnLoginComplete, L);

    assert(top == lua_gettop(L));
    return 0;
}

int Facebook_Logout(lua_State* L)
{
    int top = lua_gettop(L);
    VerifyCallback(L);

    // https://developers.facebook.com/docs/reference/javascript/FB.logout
    EM_ASM({
        FB.logout(function(response) {
            // user is now logged out
        });
    });

    assert(top == lua_gettop(L));
    return 0;
}

// TODO: Move out to common stuff (also in engine/facebook/src/facebook_android.cpp)
void AppendArray(lua_State* L, char* buffer, uint32_t buffer_size, int idx)
{
    lua_pushnil(L);
    *buffer = 0;
    while (lua_next(L, idx) != 0)
    {
        if (!lua_isstring(L, -1))
            luaL_error(L, "permissions can only be strings (not %s)", lua_typename(L, lua_type(L, -1)));
        if (*buffer != 0)
            dmStrlCat(buffer, ",", buffer_size);
        const char* permission = lua_tostring(L, -1);
        dmStrlCat(buffer, permission, buffer_size);
        lua_pop(L, 1);
    }
}


typedef void (*OnRequestReadPermissionsCallback)(void *L, const char* error);

void OnRequestReadPermissionsComplete(void* L, const char* error)
{
    RunCallback((lua_State*)L, error);
}

int Facebook_RequestReadPermissions(lua_State* L)
{
    int top = lua_gettop(L);
    VerifyCallback(L);

    luaL_checktype(L, top-1, LUA_TTABLE);
    luaL_checktype(L, top, LUA_TFUNCTION);
    lua_pushvalue(L, top);
    g_Facebook.m_Callback = luaL_ref(L, LUA_REGISTRYINDEX);

    dmScript::GetInstance(L);
    g_Facebook.m_Self = luaL_ref(L, LUA_REGISTRYINDEX);

    char permissions[512];
    AppendArray(L, permissions, 512, top-1);

    // https://developers.facebook.com/docs/reference/javascript/FB.login/v2.0
    // https://developers.facebook.com/docs/facebook-login/permissions/v2.0
    EM_ASM_ARGS({
        var permissions = $0;
        var callback    = $1;
        var lua_state   = $2;

        FB.login(function(response) {
            var e = (response && response.error ? response.error.message : 0);
            if (e == 0 && response.authResponse) {
                Runtime.dynCall('vii', callback, [lua_state, 0]);
            } else if (e != 0) {
                var buf = allocate(intArrayFromString(e), 'i8', ALLOC_STACK);
                Runtime.dynCall('vii', callback, [lua_state, buf]);
            } else {
                // No authResponse. Below text is from facebook's own example of this case.
                e = 'User cancelled login or did not fully authorize.';
                var buf = allocate(intArrayFromString(e), 'i8', ALLOC_STACK);
                Runtime.dynCall('vii', callback, [lua_state, buf]);
            }
        }, {scope: Pointer_stringify(permissions)});
    }, permissions, (OnRequestReadPermissionsCallback) OnRequestReadPermissionsComplete, L);
    assert(top == lua_gettop(L));
    return 0;
}

typedef void (*OnRequestPublishPermissionsCallback)(void *L, const char* error);

void OnRequestPublishPermissionsComplete(void* L, const char* error)
{
    RunCallback((lua_State*)L, error);
}

int Facebook_RequestPublishPermissions(lua_State* L)
{
    int top = lua_gettop(L);
    VerifyCallback(L);

    luaL_checktype(L, top-2, LUA_TTABLE);
    // Cannot find any doc that implies that audience is used in the javascript sdk...
    int audience = luaL_checkinteger(L, top-1);
    luaL_checktype(L, top, LUA_TFUNCTION);
    lua_pushvalue(L, top);
    g_Facebook.m_Callback = luaL_ref(L, LUA_REGISTRYINDEX);

    dmScript::GetInstance(L);
    g_Facebook.m_Self = luaL_ref(L, LUA_REGISTRYINDEX);

    char permissions[512];
    AppendArray(L, permissions, 512, top-2);

    // https://developers.facebook.com/docs/reference/javascript/FB.login/v2.0
    // https://developers.facebook.com/docs/facebook-login/permissions/v2.0
    EM_ASM_ARGS({
        var permissions = $0;
        var audience    = $1;
        var callback    = $2;
        var lua_state   = $3;

        FB.login(function(response) {
            var e = (response && response.error ? response.error.message : 0);
            if (e == 0 && response.authResponse) {
                Runtime.dynCall('vii', callback, [lua_state, 0]);
            } else if (e != 0) {
                var buf = allocate(intArrayFromString(e), 'i8', ALLOC_STACK);
                Runtime.dynCall('vii', callback, [lua_state, buf]);
            } else {
                // No authResponse. Below text is from facebook's own example of this case.
                e = 'User cancelled login or did not fully authorize.';
                var buf = allocate(intArrayFromString(e), 'i8', ALLOC_STACK);
                Runtime.dynCall('vii', callback, [lua_state, buf]);
            }
        }, {scope: Pointer_stringify(permissions)});
    }, permissions, audience, (OnRequestPublishPermissionsCallback) OnRequestPublishPermissionsComplete, L);

    assert(top == lua_gettop(L));
    return 0;
}


typedef void (*OnAccessTokenCallback)(void *L, const char* access_token);

void OnAccessTokenComplete(void* L, const char* access_token)
{
    if(access_token != 0)
    {
        lua_pushstring((lua_State *)L, access_token);
    }
    else
    {
        lua_pushnil((lua_State *)L);
        dmLogError("Access_token is null (logged out?).");
    }
}

int Facebook_AccessToken(lua_State* L)
{
    int top = lua_gettop(L);

    // https://developers.facebook.com/docs/reference/javascript/FB.getAuthResponse/
    EM_ASM_ARGS({
        var callback = $0;
        var lua_state = $1;

        var response = FB.getAuthResponse(); // Cached??
        var access_token = (response && response.accessToken ? response.accessToken : 0);

        if(access_token != 0) {
            var buf = allocate(intArrayFromString(access_token), 'i8', ALLOC_STACK);
            Runtime.dynCall('vii', callback, [lua_state, buf]);
        } else {
            Runtime.dynCall('vii', callback, [lua_state, 0]);
        }
    }, (OnAccessTokenCallback) OnAccessTokenComplete, L);

    assert(top + 1 == lua_gettop(L));
    return 1;
}


typedef void (*OnPermissionsCallback)(void *L, const char* json_arr);

void OnPermissionsComplete(void* L, const char* json_arr)
{
    if(json_arr != 0)
    {
        // Note that the permissionsJsonArray contains a regular string array in json format,
        // e.g. ["foo", "bar", "baz", ...]
        dmJson::Document doc;
        dmJson::Result r = dmJson::Parse(json_arr, &doc);
        if (r == dmJson::RESULT_OK && doc.m_NodeCount > 0) {
            ToLua((lua_State *)L, &doc, 0);
        } else {
            dmLogError("Failed to parse Facebook_Permissions response (%d)", r);
            lua_newtable((lua_State *)L);
        }
        dmJson::Free(&doc);
    }
    else
    {
        dmLogError("Got empty Facebook_Permissions response (or FB error).");
        // This follows the iOS implementation...
        lua_newtable((lua_State *)L);
    }
}

int Facebook_Permissions(lua_State* L)
{
    int top = lua_gettop(L);

    https://developers.facebook.com/docs/graph-api/reference/v2.0/user/permissions
    EM_ASM_ARGS({
        var callback = $0;
        var lua_state = $1;

        FB.api('/me/permissions', function (response) {
            var e = (response && response.error ? response.error.message : 0);
            if(e == 0 && response.data) {
                var permissions = [];
                for (var i=0; i<response.data.length; i++) {
                    if(response.data[i].permission && response.data[i].status) {
                        if(response.data[i].status === 'granted') {
                            permissions.push(response.data[i].permission);
                        } else if(response.data[i].status === 'declined') {
                            // TODO: Handle declined permissions?
                        }
                    }
                }
                // Just make json of the acutal permissions (array)
                var permissions_data = JSON.stringify(permissions);
                var buf = allocate(intArrayFromString(permissions_data), 'i8', ALLOC_STACK);
                Runtime.dynCall('vii', callback, [lua_state, buf]);
            } else {
                Runtime.dynCall('vii', callback, [lua_state, 0]);
            }
        });
    }, (OnPermissionsCallback) OnPermissionsComplete, L);

    assert(top + 1 == lua_gettop(L));
    return 1;
}


typedef void (*OnMeCallback)(void *L, const char* json);

void OnMeComplete(void* L, const char* json)
{
    if(json != 0)
    {
        // Note: this will pass ALL key/values back to lua, not just string types!
        dmJson::Document doc;
        dmJson::Result r = dmJson::Parse(json, &doc);
        if (r == dmJson::RESULT_OK && doc.m_NodeCount > 0) {
            ToLua((lua_State *)L, &doc, 0);
        } else {
            dmLogError("Failed to parse Facebook_Me response (%d)", r);
            lua_pushnil((lua_State *)L);
        }
        dmJson::Free(&doc);
    }
    else
    {
        // This follows the iOS implementation...
        dmLogError("Got empty Facebook_Me response (or FB error).");
        lua_pushnil((lua_State *)L);
    }
}

int Facebook_Me(lua_State* L)
{
    int top = lua_gettop(L);

    // https://developers.facebook.com/docs/graph-api/reference/v2.0/user/
    EM_ASM_ARGS({
        var callback = $0;
        var lua_state = $1;

        FB.api('/me', function (response) {
            var e = (response && response.error ? response.error.message : 0);
            if(e == 0) {
                var me_data = JSON.stringify(response);
                var buf = allocate(intArrayFromString(me_data), 'i8', ALLOC_STACK);
                Runtime.dynCall('vii', callback, [lua_state, buf]);
            } else {
                // This follows the iOS implementation...
                Runtime.dynCall('vii', callback, [lua_state, 0]);
            }
        });
    }, (OnMeCallback) OnMeComplete, L);

    assert(top + 1 == lua_gettop(L));
    return 1;
}


typedef void (*OnShowDialogCallback)(void* L, const char* url, const char* error);

void OnShowDialogComplete(void* L, const char* url, const char* error)
{
    RunDialogResultCallback((lua_State*)L, url, error);
}

int Facebook_ShowDialog(lua_State* L)
{
    int top = lua_gettop(L);
    VerifyCallback(L);

    const char* dialog = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    lua_pushvalue(L, 3);
    g_Facebook.m_Callback = luaL_ref(L, LUA_REGISTRYINDEX);
    dmScript::GetInstance(L);
    g_Facebook.m_Self = luaL_ref(L, LUA_REGISTRYINDEX);

    char params_json[1024];
    params_json[0] = '{';
    params_json[1] = '\0';
    char tmp[128];
    lua_pushnil(L);
    int i = 0;
    while (lua_next(L, 2) != 0) {
        const char* v = luaL_checkstring(L, -1);
        const char* k = luaL_checkstring(L, -2);
        DM_SNPRINTF(tmp, sizeof(tmp), "\"%s\": \"%s\"", k, v);
        if (i > 0) {
            dmStrlCat(params_json, ",", sizeof(params_json));
        }
        dmStrlCat(params_json, tmp, sizeof(params_json));
        lua_pop(L, 1);
        ++i;
    }
    dmStrlCat(params_json, "}", sizeof(params_json));

    // https://developers.facebook.com/docs/javascript/reference/FB.ui
    EM_ASM_ARGS({
        var params    = $0;
        var mth       = $1;
        var callback  = $2;
        var lua_state = $3;

        var par = JSON.parse(Pointer_stringify(params));
        par.method = Pointer_stringify(mth);

        FB.ui(par, function(response) {
            // https://developers.facebook.com/docs/graph-api/using-graph-api/v2.0
            //   (Section 'Handling Errors')
            var e = (response && response.error ? response.error.message : 0);
            if(e == 0) {
                // TODO: UTF8?
                // Matches iOS
                var result = 'fbconnect://success?';
                for (var key in response) {
                    if(response.hasOwnProperty(key)) {
                        result += key + '=' + encodeURIComponent(response[key]) + '&';
                    }
                }
                if(result[result.length-1] === '&') {
                    result.slice(0, -1);
                }
                var url = allocate(intArrayFromString(result), 'i8', ALLOC_STACK);
                Runtime.dynCall('viii', callback, [lua_state, url, e]);
            } else {
                var error = allocate(intArrayFromString(e), 'i8', ALLOC_STACK);
                var url = 0;
                Runtime.dynCall('viii', callback, [lua_state, url, error]);
            }
        });
    }, params_json, dialog, (OnShowDialogCallback) OnShowDialogComplete, L);

    assert(top == lua_gettop(L));
    return 0;
}


static const luaL_reg Facebook_methods[] =
{
    {"login", Facebook_Login},
    {"logout", Facebook_Logout},
    {"access_token", Facebook_AccessToken},
    {"permissions", Facebook_Permissions},
    {"request_read_permissions", Facebook_RequestReadPermissions},
    {"request_publish_permissions", Facebook_RequestPublishPermissions},
    {"me", Facebook_Me},
    {"show_dialog", Facebook_ShowDialog},
    {0, 0}
};

dmExtension::Result InitializeFacebook(dmExtension::Params* params)
{
    if(g_Facebook.m_Initialized)
        return dmExtension::RESULT_OK;

    // 355198514515820 is HelloFBSample. Default value in order to avoid exceptions
    // Better solution?
    g_Facebook.m_appId = dmConfigFile::GetString(params->m_ConfigFile, "facebook.appid", "355198514515820");

    // We assume that the Facebook javascript SDK is loaded by now.
    // This should be done via a script tag (synchronously) in the html page:
    // <script type="text/javascript" src="//connect/facebook.net/en_US/sdk.js"></script>
    // This script tag MUST be located before the engine (game) js script tag.
    EM_ASM_ARGS({
        var app_id = $0;

        FB.init({
            appId      : Pointer_stringify(app_id),
            status     : false,
            xfbml      : false,
            version    : 'v2.0',
        });
    }, g_Facebook.m_appId);

    lua_State* L = params->m_L;
    int top = lua_gettop(L);
    luaL_register(L, LIB_NAME, Facebook_methods);

#define SETCONSTANT(name) \
        lua_pushnumber(L, (lua_Number) name); \
        lua_setfield(L, -2, #name);\

    SETCONSTANT(STATE_CREATED);
    SETCONSTANT(STATE_CREATED_TOKEN_LOADED);
    SETCONSTANT(STATE_CREATED_OPENING);
    SETCONSTANT(STATE_OPEN);
    SETCONSTANT(STATE_OPEN_TOKEN_EXTENDED);
    SETCONSTANT(STATE_CLOSED_LOGIN_FAILED);
    SETCONSTANT(STATE_CLOSED);
    SETCONSTANT(AUDIENCE_NONE)
    SETCONSTANT(AUDIENCE_ONLYME)
    SETCONSTANT(AUDIENCE_FRIENDS)
    SETCONSTANT(AUDIENCE_EVERYONE)

    lua_pop(L, 1);
    assert(top == lua_gettop(L));

    g_Facebook.m_Initialized = true;

    return dmExtension::RESULT_OK;
}

dmExtension::Result FinalizeFacebook(dmExtension::Params* params)
{
    // TODO: "Uninit" FB SDK here?

    g_Facebook = Facebook();

    return dmExtension::RESULT_OK;
}

DM_DECLARE_EXTENSION(FacebookExt, "Facebook", 0, 0, InitializeFacebook, FinalizeFacebook)
