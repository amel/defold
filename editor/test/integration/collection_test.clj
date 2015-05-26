(ns integration.collection-test
  (:require [clojure.test :refer :all]
            [clojure.pprint :refer [pprint]]
            [dynamo.graph :as g]
            [dynamo.graph.test-support :refer [with-clean-system]]
            [dynamo.types :as t]
            [dynamo.geom :as geom]
            [dynamo.util :as util]
            [editor.atlas :as atlas]
            [editor.collection :as collection]
            [editor.core :as core]
            [editor.cubemap :as cubemap]
            [editor.game-object :as game-object]
            [editor.image :as image]
            [editor.platformer :as platformer]
            [editor.project :as project]
            [editor.scene :as scene]
            [editor.sprite :as sprite]
            [editor.switcher :as switcher]
            [editor.workspace :as workspace]
            [internal.render.pass :as pass]
            [integration.test-util :as test-util])
  (:import [dynamo.types Region]
           [java.awt.image BufferedImage]
           [java.io File]
           [javax.vecmath Point3d Matrix4d]
           [javax.imageio ImageIO]))

(defn- dump-outline [outline]
  {:self (type (:self outline)) :children (map dump-outline (:children outline))})

(deftest hierarchical-outline
  (testing "Hierarchical outline"
           (with-clean-system
             (let [workspace (test-util/setup-workspace! world)
                   project   (test-util/setup-project! workspace)
                   node      (test-util/resource-node project "/logic/hierarchy.collection")
                   outline   (g/node-value node :outline)]
               ; One game object under the collection
               (is (= 1 (count (:children outline))))
               ; One component and game object under the game object
               (is (= 2 (count (:children (first (:children outline))))))))))

(deftest hierarchical-scene
  (testing "Hierarchical scene"
           (with-clean-system
             (let [workspace (test-util/setup-workspace! world)
                   project   (test-util/setup-project! workspace)
                   node      (test-util/resource-node project "/logic/hierarchy.collection")
                   scene     (g/node-value node :scene)]
               ; One game object under the collection
               (is (= 1 (count (:children scene))))
               ; One component and game object under the game object
               (is (= 2 (count (:children (first (:children scene))))))))))
