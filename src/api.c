#include "api.h"
#include "item.h"
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

#define MAX_FILENAME_LENGTH 1024
#define MAX_ERROR_SIZE 255

#define MODULE_DIR "modules/"

// API functions
int api_add_block_type(lua_State *l)
{
    // add the given 'block type' to our engine and return the generated block ID
    const char *block_name = lua_tostring(l, 1);

    // retrieve table
    int tiles[7] = {0};
    luaL_checktype(l, 2, LUA_TTABLE);
    lua_pushnil(l);
    int i = 0;
    while (lua_next(l, 2) != 0) {
       tiles[i] = lua_tonumber(l, -1);

       lua_pop(l, 1);
        i++;
    }

    bool is_plant = lua_toboolean(l, 3);
    bool is_obstacle = lua_toboolean(l, 4);
    bool is_transparent = lua_toboolean(l, 5);
    bool is_destructable = lua_toboolean(l, 6);

    int block_id = add_new_item(block_name, tiles, is_plant, is_obstacle, is_transparent, is_destructable);
    lua_pushnumber(l, block_id);
    return 1;
}

luaL_Reg clua_regs[] =
{
    { "add_block_type", api_add_block_type },
    { NULL, NULL }
};

// structs
struct module_name_list {
    char *filename;
    struct module_name_list *next_ptr;
};

struct loaded_module_list {
    char *filename;
    lua_State *L;
    struct loaded_module_list *next_ptr;
};

struct loaded_module_list *loaded_mods;

// lua init/shutdown
int clua_init()
{
    struct module_name_list *mod_n, *mod_names;
    mod_names = NULL;
    loaded_mods = NULL;

    // get list of modules
#ifdef _WIN32
    WIN32_FIND_DATA fdFile;
    HANDLE hFind = NULL;

    char sPath[2048];

    sprintf(sPath, "%s\\*.lua", MODULE_DIR);

    if((hFind = FindFirstFile(sPath, &fdFile)) == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[Lua] Module folder not found: [%s]\n", MODULE_DIR);
        return 1;
    }

    do {
        if(strcmp(fdFile.cFileName, ".") != 0
           && strcmp(fdFile.cFileName, "..") != 0)
        {
            char *module_filename = calloc(MAX_FILENAME_LENGTH, sizeof(char));

            if (MAX_FILENAME_LENGTH <= (strlen(MODULE_DIR) + strlen(fdFile.cFileName) + 1)) {
                fprintf(stderr, "[Lua] Module name is too large: %s\n", fdFile.cFileName);
                continue;
            }

            snprintf(module_filename, MAX_FILENAME_LENGTH - 1, "%s\\%s", MODULE_DIR, fdFile.cFileName);

            mod_n = malloc(sizeof(struct module_name_list));
            mod_n->filename = module_filename;
            mod_n->next_ptr = NULL;

            SGLIB_LIST_ADD(struct module_name_list, mod_names, mod_n, next_ptr);
        }
    } while (FindNextFile(hFind, &fdFile));

    FindClose(hFind);
#else
    DIR *d;
    struct dirent *dir;
    d = opendir(MODULE_DIR);

    if (d) {
        char *ext = ".lua";
        int ext_len = strlen(ext);

        while ((dir = readdir(d)) != NULL) {
            int name_len = strlen(dir->d_name);

            if ((name_len > ext_len) && (0 == strncmp(dir->d_name + (name_len - ext_len), ext, ext_len))) {
                char *module_filename = calloc(MAX_FILENAME_LENGTH, sizeof(char));

                if (MAX_FILENAME_LENGTH <= (strlen(MODULE_DIR) + strlen(dir->d_name) + 1)) {
                    fprintf(stderr, "[Lua] Module name is too large: %s\n", dir->d_name);
                    continue;
                }

                strncpy(module_filename, MODULE_DIR, MAX_FILENAME_LENGTH - 1);
                strncat(module_filename, dir->d_name, MAX_FILENAME_LENGTH - strlen(MODULE_DIR) - 1);

                mod_n = malloc(sizeof(struct module_name_list));
                mod_n->filename = module_filename;
                mod_n->next_ptr = NULL;

                SGLIB_LIST_ADD(struct module_name_list, mod_names, mod_n, next_ptr);
            }
        }

        closedir(d);
    }
#endif

    // load lua modules
    struct loaded_module_list *mod; // temp

    SGLIB_LIST_MAP_ON_ELEMENTS(struct module_name_list, mod_names, mod_n, next_ptr, {
        printf("[Lua] Loading module %s\n", mod_n->filename);

        mod = malloc(sizeof(struct loaded_module_list));

        // we have a separate lua state for every single module we load
        mod->L = luaL_newstate();
        mod->next_ptr = NULL;
        luaL_openlibs(mod->L);

        // init our API
        lua_newtable(mod->L);
        luaL_setfuncs(mod->L, clua_regs, 0);
        lua_setglobal(mod->L, "api");

        if (luaL_loadfile(mod->L, mod_n->filename)) {
            fprintf(stderr, "[Lua] Module %s failed to load\n", mod_n->filename);
            free(mod);
        }
        else {
            if (lua_pcall(mod->L, 0, 0, 0)) {
                fprintf(stderr, "[Lua] Module %s failed to run\n", mod_n->filename);
                free(mod);
            }
            else {
                lua_getglobal(mod->L, "startup");

                if (lua_pcall(mod->L, 0, 0, 0)) {
                    fprintf(stderr, "[Lua] Startup in %s failed\n", mod_n->filename);
                    free(mod);
                }
                else {
                    SGLIB_LIST_ADD(struct loaded_module_list, loaded_mods, mod, next_ptr)
                }
            }
        }

        // cleanup module names
        free(mod_n);
    });

    return 0;
}


int clua_close()
{
    // close lua on every loaded module
    struct loaded_module_list *mod; // temp

    SGLIB_LIST_MAP_ON_ELEMENTS(struct loaded_module_list, loaded_mods, mod, next_ptr, {
        lua_getglobal(mod->L, "shutdown");

        if (lua_pcall(mod->L, 0, 0, 0)) {
            fprintf(stderr, "[Lua] Shutdown in %s failed\n", mod->filename);
            free(mod);
        }

        lua_close(mod->L);
        free(mod);
    });

    return 0;
}
