#pragma once

#include "types.h"

enum Tag_t : S16{
    TAB_PLAYER_STOPS_COASTING_BLOCK, //
    TAG_PLAYER_PUSHES_MORE_THAN_ONE_MASS, //
    TAG_BLOCK, //
    TAG_BLOCK_FALLS_IN_PIT, //
    TAG_BLOCK_BLOCKS_ICE_FROM_BEING_MELTED, //
    TAG_BLOCK_BLOCKS_ICE_FROM_BEING_SPREAD, //
    TAB_BLOCK_MOMENTUM_COLLISION, //
    TAG_BLOCK_GETS_SPLIT, //
    TAG_BLOCK_GETS_DESTROYED, //
    TAG_BLOCKS_STACKED, //
    TAG_BLOCK_EXTINGUISHED_BY_STOMP, //
    TAG_BLOCK_BOUNCES_BACK_FROM_MOMENTUM,
    TAG_BLOCK_SQUISHES_PLAYER, //
    TAG_BLOCK_MULTIPLE_INTERVALS_TO_STOP, //
    TAG_SPLIT_BLOCK, //
    TAG_POPUP, //
    TAB_POPUP_RAISES_BLOCK, //
    TAB_POPUP_RAISES_PLAYER, //
    TAG_LEVER, //
    TAG_PIT, //
    TAG_PRESSURE_PLATE, //
    TAG_LIGHT_DETECTOR,
    TAG_ICE, //
    TAG_ICE_DETECTOR, //
    TAG_ICE_BLOCK, //
    TAG_FIRE_BLOCK, //
    TAG_FIRE_BLOCK_SLIDES_ON_ICE,
    TAG_ICED_BLOCK, //
    TAG_MELT_ICE, //
    TAG_MELTED_POPUP, //
    TAG_MELTED_PRESSURE_PLATE, //
    TAG_SPREAD_ICE, //
    TAG_ARROW, //
    TAG_ARROW_CHANGES_BLOCK_ELEMENT, //
    TAB_ARROW_ACTIVATES_LEVER, //
    TAG_ARROW_STICKS_INTO_BLOCK, //
    TAG_ICED_POPUP, //
    TAG_ICED_PRESSURE_PLATE, //
    TAG_ICED_PRESSURE_PLATE_NOT_ACTIVATED,
    TAG_PORTAL, //
    TAG_PORTAL_ROT_90, //
    TAG_PORTAL_ROT_180, //
    TAG_PORTAL_ROT_270, //
    TAG_TELEPORT_PLAYER, //
    TAG_TELEPORT_BLOCK, //
    TAG_TELEPORT_ARROW, //
    TAG_BLOCK_GETS_ENTANGLED, //
    TAG_PLAYER_GETS_ENTANGLED, //
    TAG_ARROW_GETS_ENTANGLED,
    TAG_ENTANGLED_BLOCK, //
    TAG_ENTANGLED_BLOCK_ROT_90, //
    TAG_ENTANGLED_BLOCK_ROT_180, //
    TAG_ENTANGLED_BLOCK_ROT_270, //
    TAG_ENTANGLED_BLOCK_HELD_DOWN_UNMOVABLE, //
    TAG_ENTANGLED_BLOCK_FLOATS, //
    TAG_ENTANGLED_BLOCKS_OF_DIFFERENT_SPLITS, //
    TAG_ENTANGLED_CENTROID_COLLISION, //
    TAG_THREE_PLUS_BLOCKS_ENTANGLED, //
    TAG_THREE_PLUS_PLAYERS_ENTANGLED, //
    TAG_THREE_PLUS_ARROWS_ENTANGLED,
    TAG_COUNT
};

const char* tag_to_string(Tag_t tag);

void add_global_tag(Tag_t tag);
bool* get_global_tags();
void clear_global_tags();
void log_global_tags();
