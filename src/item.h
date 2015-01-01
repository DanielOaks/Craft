#ifndef _item_h_
#define _item_h_

#include <stdbool.h>
#include <sglib.h>

// structs
struct tile_ids {
    // these are for usual blocks such as grass, sand, etc
    int top;
    int bottom;
    int left;
    int right;
    int front;
    int back;

    // these are for plants and such, that require two sprites in the center of a block
    int sprite;
};

struct item_list {
    const char *name;
    int id;
    struct tile_ids *tile;

    bool is_plant;
    bool is_obstacle;
    bool is_transparent;
    bool is_destructable;

    struct item_list *next_ptr;
};

// functions
void setup_base_items();

int add_new_item(const char *name, int tile[7], bool is_plant, bool is_obstacle, bool is_transparent, bool is_destructable); // returns allocated item id
struct item_list *get_item_by_id(unsigned int id);
struct item_list *get_item_by_name(const char *name);

bool is_plant(int item_id);
bool is_obstacle(int item_id);
bool is_transparent(int item_id);
bool is_destructable(int item_id);

// global data
struct item_list *items;
int last_item_id;

#endif
