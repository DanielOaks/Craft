#include "config.h"
#include "noise.h"
#include "world.h"
#include "item.h"

void create_world(int p, int q, world_func func, void *arg) {
    int pad = 1;

    // block ids
    itemid grass_block_id = get_item_by_name("grass")->id;
    itemid sand_block_id = get_item_by_name("sand")->id;
    itemid cobble_block_id = get_item_by_name("cobble")->id;
    itemid sandstone_block_id = cobble_block_id; // til we create a real sandstone block
    itemid first_flower_block_id = get_item_by_name("yellow flower")->id;
    itemid tall_grass_id = get_item_by_name("tall grass")->id;

    // chunk creation loop
    for (int dx = -pad; dx < CHUNK_SIZE + pad; dx++) {
        for (int dz = -pad; dz < CHUNK_SIZE + pad; dz++) {
            int flag = 1;
            // TODO: find out what this meant to be for, check shaders maybe?
            // if (dx < 0 || dz < 0 || dx >= CHUNK_SIZE || dz >= CHUNK_SIZE) {
            //     flag = -1;
            // }
            int x = p * CHUNK_SIZE + dx;
            int z = q * CHUNK_SIZE + dz;
            float f = simplex2(x * 0.01, z * 0.01, 4, 0.2, 2);
            float g = simplex2(-x * 0.01, -z * 0.01, 2, 0.3, 2);
            int mh = g * 32 + 16;
            int h = f * mh;
            itemid w = grass_block_id;
            itemid underblock_id = cobble_block_id;
            int underblock_level = 3;
            int t = 4;
            if (h <= t) {
                h = t;
                w = sand_block_id;
                underblock_id = sandstone_block_id;
                underblock_level = 1;
            }

            // base layer of stone
            for (int y = 0; y < underblock_level; y++) {
                func(x, y, z, cobble_block_id, arg);
            }
            // sand and grass terrain
            for (int y = underblock_level; y < h; y++) {
                func(x, y, z, w * flag, arg);
            }

            if (w == grass_block_id) {
                if (SHOW_PLANTS) {
                    // tall grass
                    if (simplex2(-x * 0.1, z * 0.1, 4, 0.8, 2) > 0.6) {
                        w = tall_grass_id;
                        func(x, h, z, w * flag, arg);
                    }
                    // flowers
                    if (simplex2(x * 0.05, -z * 0.05, 4, 0.8, 2) > 0.7) {
                        w = first_flower_block_id + simplex2(x * 0.1, z * 0.1, 4, 0.8, 2) * 7;
                        func(x, h, z, w * flag, arg);
                    }
                }
                // trees
                int ok = SHOW_TREES;
                if (dx - 4 < 0 || dz - 4 < 0 ||
                    dx + 4 >= CHUNK_SIZE || dz + 4 >= CHUNK_SIZE)
                {
                    ok = 0;
                }
                if (ok && simplex2(x, z, 6, 0.5, 2) > 0.84) {
                    for (int y = h + 3; y < h + 8; y++) {
                        for (int ox = -3; ox <= 3; ox++) {
                            for (int oz = -3; oz <= 3; oz++) {
                                int d = (ox * ox) + (oz * oz) +
                                    (y - (h + 4)) * (y - (h + 4));
                                if (d < 11 && simplex3(x + ox, y, z + oz, 4, 0.3, 2) > 0.4) {
                                    func(x + ox, y, z + oz, 15, arg);
                                }
                            }
                        }
                    }
                    for (int y = h; y < h + 7; y++) {
                        func(x, y, z, 5, arg);
                    }
                }
            }
        }
    }
}
