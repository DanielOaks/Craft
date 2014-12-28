#include "item.h"
#include "db.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

struct item_list *items;

// holds the id of the 'last' item we have
// this gets recalced every time we add or subtract items
int last_item_id;

void _recalc_last_item_id() {
    last_item_id = 0;

    struct item_list *it = NULL;
    SGLIB_LIST_MAP_ON_ELEMENTS(struct item_list, items, it, next_ptr, {
        if (it->id > last_item_id) {
            last_item_id = it->id;
        }
    });
}

// we do lots and lots of lookups by id for our item list, mostly for recalcing the world
// doing this by standard linked-list traversal is super slow, so we instead use an
//   index-based (eg: [2], [37], etc) cache internally to access things by id faster
struct item_list **_item_id_lookup_cache;

// recalculate all we need for id-based lookup cache
void _recalc_item_id_lookup_cache() {
    // free old space
    if (_item_id_lookup_cache != NULL) {
        free(_item_id_lookup_cache);
    }

    // create new space
    _item_id_lookup_cache = calloc(sizeof(struct item_list*), last_item_id + 1);

    // and allocate required id mappings with standard linked-list traversal
    struct item_list *it = NULL;
    SGLIB_LIST_MAP_ON_ELEMENTS(struct item_list, items, it, next_ptr, {
        _item_id_lookup_cache[it->id] = it;
    });
}

struct item_list *get_item_by_id(unsigned int id) {
    // cache is only valid for ids >= last_item_id, so we check that here
    if (id <= last_item_id) {
        return _item_id_lookup_cache[id];
    }
    return NULL;
}

// we still do linked-list traversal for accessing by name, but we assume modules cache
//   the ids of their required items anyways. if required, do this name-based lookup by
//   a hash-map in the future
struct item_list *get_item_by_name(char *name) {
    SGLIB_LIST_MAP_ON_ELEMENTS(struct item_list, items, item, next_ptr, {
        if (strncmp(item->name, name, sizeof(item->name) / sizeof(char)) == 0) {
            return item;
        }
    });
    return NULL;
}

// adds a new item and returns allocated item id
// also recalcs caches as required above
int add_new_item(const char *name, int tile[7], bool is_plant, bool is_obstacle, bool is_transparent, bool is_destructable) {
    // create tiles
    struct tile_ids *tiles = malloc(sizeof(struct tile_ids));
    tiles->top = tile[0];
    tiles->bottom = tile[1];
    tiles->left = tile[2];
    tiles->right = tile[3];
    tiles->front = tile[4];
    tiles->back = tile[5];
    tiles->sprite = tile[6];

    // create item
    struct item_list *new_item = malloc(sizeof(struct item_list));
    new_item->name = name;
    new_item->next_ptr = NULL;
    new_item->id = last_item_id + 1;
    new_item->tile = tiles;
    new_item->is_plant = is_plant;
    new_item->is_obstacle = is_obstacle;
    new_item->is_transparent = is_transparent;
    new_item->is_destructable = is_destructable;

    db_insert_item(new_item->id, new_item->name);

    // add item to our internal list
    if (items == NULL) {
        items = new_item;
    }
    else {
        SGLIB_LIST_ADD(struct item_list, items, new_item, next_ptr);
    }

    // recalculate item caches
    _recalc_last_item_id();
    _recalc_item_id_lookup_cache();

    return new_item->id;
}

// initializing our base items
void setup_base_items() {
    items = NULL;
    _item_id_lookup_cache = NULL;
    last_item_id = -1; // must be -1 because add_new_item calcs new item id as this + 1

    // face tile arrays -> (top, bottom, left, right, front, back, sprite)
    add_new_item("empty", (int[7]){0, 0, 0, 0, 0, 0, 0}, false, false, true, false);
    add_new_item("grass", (int[7]){32, 0, 16, 16, 16, 16, 0}, false, true, false, true);
    add_new_item("sand", (int[7]){1, 1, 1, 1, 1, 1, 0}, false, true, false, true);
    add_new_item("stone", (int[7]){2, 2, 2, 2, 2, 2, 0}, false, true, false, true);
    add_new_item("brick", (int[7]){3, 3, 3, 3, 3, 3, 0}, false, true, false, true);
    add_new_item("wood", (int[7]){36, 4, 20, 20, 20, 20, 0}, false, true, false, true);
    add_new_item("cement", (int[7]){5, 5, 5, 5, 5, 5, 0}, false, true, false, true);
    add_new_item("dirt", (int[7]){6, 6, 6, 6, 6, 6, 0}, false, true, false, true);
    add_new_item("plank", (int[7]){7, 7, 7, 7, 7, 7, 0}, false, true, false, true);
    add_new_item("snow", (int[7]){40, 8, 24, 24, 24, 24, 0}, false, true, false, true);
    add_new_item("glass", (int[7]){9, 9, 9, 9, 9, 9, 0}, false, true, true, true);
    add_new_item("cobble", (int[7]){10, 10, 10, 10, 10, 10, 0}, false, true, false, true);
    add_new_item("light stone", (int[7]){11, 11, 11, 11, 11, 11, 0}, false, true, false, true);
    add_new_item("dark stone", (int[7]){12, 12, 12, 12, 12, 12, 0}, false, true, false, true);
    add_new_item("chest", (int[7]){13, 13, 13, 13, 13, 13, 0}, false, true, false, true);
    add_new_item("leaves", (int[7]){14, 14, 14, 14, 14, 14, 0}, false, true, true, true);
    add_new_item("cloud", (int[7]){15, 15, 15, 15, 15, 15, 0}, false, false, true, false);
    add_new_item("tall grass", (int[7]){0, 0, 0, 0, 0, 0, 48}, true, false, true, true);
    add_new_item("yellow flower", (int[7]){0, 0, 0, 0, 0, 0, 49}, true, false, true, true);
    add_new_item("red flower", (int[7]){0, 0, 0, 0, 0, 0, 50}, true, false, true, true);
    add_new_item("purple flower", (int[7]){0, 0, 0, 0, 0, 0, 51}, true, false, true, true);
    add_new_item("sun flower", (int[7]){0, 0, 0, 0, 0, 0, 52}, true, false, true, true);
    add_new_item("white flower", (int[7]){0, 0, 0, 0, 0, 0, 53}, true, false, true, true);
    add_new_item("blue flower", (int[7]){0, 0, 0, 0, 0, 0, 54}, true, false, true, true);
    add_new_item("color00", (int[7]){176, 176, 176, 176, 176, 176, 0}, false, true, false, true);
    add_new_item("color01", (int[7]){177, 177, 177, 177, 177, 177, 0}, false, true, false, true);
    add_new_item("color02", (int[7]){178, 178, 178, 178, 178, 178, 0}, false, true, false, true);
    add_new_item("color03", (int[7]){179, 179, 179, 179, 179, 179, 0}, false, true, false, true);
    add_new_item("color04", (int[7]){180, 180, 180, 180, 180, 180, 0}, false, true, false, true);
    add_new_item("color05", (int[7]){181, 181, 181, 181, 181, 181, 0}, false, true, false, true);
    add_new_item("color06", (int[7]){182, 182, 182, 182, 182, 182, 0}, false, true, false, true);
    add_new_item("color07", (int[7]){183, 183, 183, 183, 183, 183, 0}, false, true, false, true);
    add_new_item("color08", (int[7]){184, 184, 184, 184, 184, 184, 0}, false, true, false, true);
    add_new_item("color09", (int[7]){185, 185, 185, 185, 185, 185, 0}, false, true, false, true);
    add_new_item("color10", (int[7]){186, 186, 186, 186, 186, 186, 0}, false, true, false, true);
    add_new_item("color11", (int[7]){187, 187, 187, 187, 187, 187, 0}, false, true, false, true);
    add_new_item("color12", (int[7]){188, 188, 188, 188, 188, 188, 0}, false, true, false, true);
    add_new_item("color13", (int[7]){189, 189, 189, 189, 189, 189, 0}, false, true, false, true);
    add_new_item("color14", (int[7]){190, 190, 190, 190, 190, 190, 0}, false, true, false, true);
    add_new_item("color15", (int[7]){191, 191, 191, 191, 191, 191, 0}, false, true, false, true);
    add_new_item("color16", (int[7]){192, 192, 192, 192, 192, 192, 0}, false, true, false, true);
    add_new_item("color17", (int[7]){193, 193, 193, 193, 193, 193, 0}, false, true, false, true);
    add_new_item("color18", (int[7]){194, 194, 194, 194, 194, 194, 0}, false, true, false, true);
    add_new_item("color19", (int[7]){195, 195, 195, 195, 195, 195, 0}, false, true, false, true);
    add_new_item("color20", (int[7]){196, 196, 196, 196, 196, 196, 0}, false, true, false, true);
    add_new_item("color21", (int[7]){197, 197, 197, 197, 197, 197, 0}, false, true, false, true);
    add_new_item("color22", (int[7]){198, 198, 198, 198, 198, 198, 0}, false, true, false, true);
    add_new_item("color23", (int[7]){199, 199, 199, 199, 199, 199, 0}, false, true, false, true);
    add_new_item("color24", (int[7]){200, 200, 200, 200, 200, 200, 0}, false, true, false, true);
    add_new_item("color25", (int[7]){201, 201, 201, 201, 201, 201, 0}, false, true, false, true);
    add_new_item("color26", (int[7]){202, 202, 202, 202, 202, 202, 0}, false, true, false, true);
    add_new_item("color27", (int[7]){203, 203, 203, 203, 203, 203, 0}, false, true, false, true);
    add_new_item("color28", (int[7]){204, 204, 204, 204, 204, 204, 0}, false, true, false, true);
    add_new_item("color29", (int[7]){205, 205, 205, 205, 205, 205, 0}, false, true, false, true);
    add_new_item("color30", (int[7]){206, 206, 206, 206, 206, 206, 0}, false, true, false, true);
    add_new_item("color31", (int[7]){207, 207, 207, 207, 207, 207, 0}, false, true, false, true);
    add_new_item("error", (int[7]){30, 30, 30, 30, 30, 30, 0}, false, true, true, true);
}

// is_* convenience functions
bool is_plant(int item_id) {
    item_id = ABS(item_id); // TODO: why are we getting negative item ids?
    struct item_list *item = get_item_by_id(item_id);

    if (item != NULL) {
        return item->is_plant;
    }

    return false;
}

bool is_obstacle(int item_id) {
    item_id = ABS(item_id); // TODO: why are we getting negative item ids?
    struct item_list *item = get_item_by_id(item_id);

    if (item != NULL) {
        return item->is_obstacle;
    }

    return false;
}

bool is_transparent(int item_id) {
    item_id = ABS(item_id); // TODO: why are we getting negative item ids?
    struct item_list *item = get_item_by_id(item_id);

    if (item != NULL) {
        return item->is_transparent;
    }

    return false;
}

bool is_destructable(int item_id) {
    item_id = ABS(item_id); // TODO: why are we getting negative item ids?
    struct item_list *item = get_item_by_id(item_id);

    if (item != NULL) {
        return item->is_destructable;
    }

    return false;
}
