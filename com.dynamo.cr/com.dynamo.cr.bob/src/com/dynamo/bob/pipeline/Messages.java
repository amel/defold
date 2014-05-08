package com.dynamo.bob.pipeline;

import com.dynamo.bob.util.BobNLS;

public class Messages extends BobNLS {
    private static final String BUNDLE_NAME = "com.dynamo.bob.pipeline.messages"; //$NON-NLS-1$

    public static String BuilderUtil_EMPTY_RESOURCE;
    public static String BuilderUtil_MISSING_RESOURCE;

    public static String GuiBuilder_MISSING_TEXTURE;
    public static String GuiBuilder_MISSING_FONT;
    public static String GuiBuilder_MISSING_LAYER;

    public static String GuiBuilder_DUPLICATED_TEXTURE;
    public static String GuiBuilder_DUPLICATED_FONT;
    public static String GuiBuilder_DUPLICATED_LAYER;

    public static String TileSetBuilder_MISSING_IMAGE_AND_COLLISION;

    static {
        // initialize resource bundle
        BobNLS.initializeMessages(BUNDLE_NAME, Messages.class);
    }

    private Messages() {
    }
}
