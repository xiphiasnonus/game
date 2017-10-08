#include "direction.h"
#include "defines.h"

#include <cassert>

DirectionMask_t g_direction_mask_conversion[] = {
     DIRECTION_MASK_LEFT,
     DIRECTION_MASK_UP,
     DIRECTION_MASK_RIGHT,
     DIRECTION_MASK_DOWN,
     DIRECTION_MASK_NONE,
};

bool direction_in_mask(DirectionMask_t mask, Direction_t dir){
     if(g_direction_mask_conversion[dir] & mask){
          return true;
     }

     return false;
}

DirectionMask_t direction_to_direction_mask(Direction_t dir){
     assert(dir <= DIRECTION_COUNT);
     return g_direction_mask_conversion[dir];
}

DirectionMask_t direction_mask_add(DirectionMask_t a, DirectionMask_t b){
     return (DirectionMask_t)(a | b); // C++ makes this annoying
}

DirectionMask_t direction_mask_add(DirectionMask_t a, int b){
     return (DirectionMask_t)(a | b); // C++ makes this annoying
}

DirectionMask_t direction_mask_add(DirectionMask_t mask, Direction_t dir){
     return (DirectionMask_t)(mask | direction_to_direction_mask(dir)); // C++ makes this annoying
}

DirectionMask_t direction_mask_remove(DirectionMask_t a, DirectionMask_t b){
     return (DirectionMask_t)(a & ~b); // C++ makes this annoying
}

DirectionMask_t direction_mask_remove(DirectionMask_t a, int b){
     return (DirectionMask_t)(a & ~b); // C++ makes this annoying
}

DirectionMask_t direction_mask_remove(DirectionMask_t mask, Direction_t dir){
     return (DirectionMask_t)(mask & ~direction_to_direction_mask(dir)); // C++ makes this annoying
}

Direction_t direction_opposite(Direction_t dir){
     return (Direction_t)(((int)(dir) + 2) % DIRECTION_COUNT);
}

bool direction_is_horizontal(Direction_t dir){
     return dir == DIRECTION_LEFT || dir == DIRECTION_RIGHT;
}

U8 direction_rotations_between(Direction_t a, Direction_t b){
     if(a < b){
          return ((int)(a) + DIRECTION_COUNT) - (int)(b);
     }

     return (int)(a) - (int)(b);
}

Direction_t direction_rotate_clockwise(Direction_t dir){
     U8 rot = (U8)(dir) + 1;
     rot %= DIRECTION_COUNT;
     return (Direction_t)(rot);
}

Direction_t direction_rotate_clockwise(Direction_t dir, U8 times){
     for(U8 i = 0; i < times; ++i){
          dir = direction_rotate_clockwise(dir);
     }

     return dir;
}

DirectionMask_t direction_mask_rotate_clockwise(DirectionMask_t mask){
     // TODO: could probably just shift?
     S8 rot = DIRECTION_MASK_NONE;

     if(mask & DIRECTION_MASK_LEFT) rot |= DIRECTION_MASK_UP;
     if(mask & DIRECTION_MASK_UP) rot |= DIRECTION_MASK_RIGHT;
     if(mask & DIRECTION_MASK_RIGHT) rot |= DIRECTION_MASK_DOWN;
     if(mask & DIRECTION_MASK_DOWN) rot |= DIRECTION_MASK_LEFT;

     return (DirectionMask_t)(rot);
}

DirectionMask_t direction_mask_flip_horizontal(DirectionMask_t mask){
     S8 flip = DIRECTION_MASK_NONE;

     if(mask & DIRECTION_MASK_LEFT) flip |= DIRECTION_MASK_RIGHT;
     if(mask & DIRECTION_MASK_RIGHT) flip |= DIRECTION_MASK_LEFT;

     // keep the vertical components the same
     if(mask & DIRECTION_MASK_UP) flip |= DIRECTION_MASK_UP;
     if(mask & DIRECTION_MASK_DOWN) flip |= DIRECTION_MASK_DOWN;

     return (DirectionMask_t)(flip);
}

DirectionMask_t direction_mask_flip_vertical(DirectionMask_t mask){
     S8 flip = DIRECTION_MASK_NONE;

     if(mask & DIRECTION_MASK_UP) flip |= DIRECTION_MASK_DOWN;
     if(mask & DIRECTION_MASK_DOWN) flip |= DIRECTION_MASK_UP;

     // keep the horizontal components the same
     if(mask & DIRECTION_MASK_LEFT) flip |= DIRECTION_MASK_LEFT;
     if(mask & DIRECTION_MASK_RIGHT) flip |= DIRECTION_MASK_RIGHT;

     return (DirectionMask_t)(flip);
}

const char* direction_to_string(Direction_t dir){
     switch(dir){
     default:
          break;
     CASE_ENUM_RET_STR(DIRECTION_LEFT)
     CASE_ENUM_RET_STR(DIRECTION_UP)
     CASE_ENUM_RET_STR(DIRECTION_RIGHT)
     CASE_ENUM_RET_STR(DIRECTION_DOWN)
     CASE_ENUM_RET_STR(DIRECTION_COUNT)
     }

     return "DIRECTION_UNKNOWN";
}