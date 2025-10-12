// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "unique_gear_midnight.hpp"

#include "action/absorb.hpp"
#include "action/action.hpp"
#include "action/dot.hpp"
#include "buff/buff.hpp"
#include "dbc/data_enums.hh"
#include "dbc/item_database.hpp"
#include "dbc/spell_data.hpp"
#include "item/item.hpp"
#include "player/actor_target_data.hpp"
#include "player/consumable.hpp"
#include "player/darkmoon_deck.hpp"
#include "player/ground_aoe.hpp"
#include "player/pet_spawner.hpp"
#include "set_bonus.hpp"
#include "sim/cooldown.hpp"
#include "sim/proc_rng.hpp"
#include "sim/sim.hpp"
#include "unique_gear.hpp"
#include "unique_gear_helper.hpp"

namespace unique_gear::midnight
{
std::vector<unsigned> __mid_special_effect_ids;
wowv_t version_min = { 12 };
wowv_t version_max = { UINT8_MAX };

void reset_version_check()
{ version_min = { 12 }; version_max = { UINT8_MAX }; }

void set_min_version( wowv_t build )
{ version_min = build; }

void set_max_version( wowv_t build )
{ version_max = build; }

// assuming priority for highest/lowest secondary is vers > mastery > haste > crit
static constexpr std::array<stat_e, 4> secondary_ratings = { STAT_VERSATILITY_RATING, STAT_MASTERY_RATING,
                                                             STAT_HASTE_RATING, STAT_CRIT_RATING };

// can be called via unqualified lookup
void register_special_effect( unsigned spell_id, custom_cb_t init_callback, bool fallback = false )
{
  unique_gear::register_special_effect( spell_id, init_callback, fallback, version_min, version_max );
  __mid_special_effect_ids.push_back( spell_id );
}

void register_special_effect( std::initializer_list<unsigned> spell_ids, custom_cb_t init_callback,
                              bool fallback = false )
{
  for ( auto id : spell_ids )
    register_special_effect( id, init_callback, fallback );
}

namespace consumables
{

}

namespace enchants
{

}

namespace embellishments
{
void arcanoweave_lining( special_effect_t& effect )
{

}

void sunfire_silk_lining( special_effect_t& effect )
{

}

void devouring_banding( special_effect_t& effect )
{

}

void blessed_pango_charm( special_effect_t& effect )
{

}

void primal_spore_binding( special_effect_t& effect )
{

}

void lucky_keychain( special_effect_t& effect )
{

}

void prismatic_focusing_iris( special_effect_t& effect )
{

}

void stabilizing_gemstone_bandolier( special_effect_t& effect )
{

}
}

namespace trinkets
{

}

namespace weapons
{

}

namespace armors
{

}

namespace sets
{

}

void register_special_effects()
{
  // NOTE: use unique_gear:: namespace for static consumables so we don't activate them with enable_all_item_effects
  // Food
  // Flasks
  // Potions
  // Oils
  // Enchants & gems
  // Embellishments & Tinkers
  register_special_effect( 1229511, embellishments::arcanoweave_lining );
  register_special_effect( 1241711, embellishments::sunfire_silk_lining );
  register_special_effect( 1244238, embellishments::devouring_banding );
  register_special_effect( 1244243, embellishments::blessed_pango_charm );
  register_special_effect( 1244276, embellishments::primal_spore_binding );
  register_special_effect( 1246298, embellishments::lucky_keychain );
  register_special_effect( 1251906, embellishments::prismatic_focusing_iris );
  register_special_effect( 1251905, embellishments::stabilizing_gemstone_bandolier );
  // Trinkets
  // Weapons
  // Armor
  // Sets
}

void register_target_data_initializers( sim_t& )
{}

void register_hotfixes()
{}

action_t* create_action( player_t* /*p*/ , std::string_view /*name*/, std::string_view /*opt*/ )
{
  return nullptr;
}
}  // namespace unique_gear::midnight
