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

// from item_naming.inc
enum gem_color_e : unsigned
{
  GEM_PERIDOT = 14262,
  GEM_GARNET = 14276,
  GEM_LAPIS = 14277,
  GEM_AMETHYST = 14278,
  GEM_DIAMOND = 14279
};

static constexpr unsigned __gem_colors[] = { GEM_PERIDOT, GEM_GARNET, GEM_LAPIS, GEM_AMETHYST };
static constexpr util::span<const unsigned> gem_colors = util::make_span( __gem_colors );

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
// 1229511 driver
// 1229746 buff
// 1230205 area trigger
void arcanoweave_lining( special_effect_t& effect )
{
  effect.player->sim->error( error_level_e::PLACEHOLDER, "midnight.arcanoweave_lining_uptime set to 0.9" );

  auto buff_amount = effect.driver()->effectN( 1 ).average( effect );

  struct arcanoweave_insight_buff_t : public stat_buff_t
  {
    rng::truncated_gauss_t interval;

    arcanoweave_insight_buff_t( const special_effect_t& e )
      : stat_buff_t( e.player, "arcanoweave_insight", e.player->find_spell( 1229746 ) ),
        interval( e.player->midnight_opts.arcanoweave_lining_update_interval,
                  e.player->midnight_opts.arcanoweave_lining_update_stddev )
    {
      set_constant_behavior( buff_constant_behavior::NEVER_CONSTANT );
    }
  };

  auto uptime_pct = effect.player->midnight_opts.arcanoweave_lining_uptime;
  if ( uptime_pct <= 0.0 )
    return;

  if ( auto buff = buff_t::find( effect.player, "arcanoweave_insight" ) )
  {
    // add stat from 2nd copy of embellishment
    debug_cast<arcanoweave_insight_buff_t*>( buff )->add_stat_from_effect_type( A_MOD_STAT, buff_amount );
    return;
  }

  auto insight = make_buff<arcanoweave_insight_buff_t>( effect );
  insight->add_stat_from_effect_type( A_MOD_STAT, buff_amount );

  if ( uptime_pct >= 1.0 )
  {
    effect.player->register_precombat_begin( [ insight ]( player_t* p ) { insight->trigger(); } );
  }
  else
  {
    effect.player->register_precombat_begin( [ insight, uptime_pct ]( player_t* p ) {
      insight->trigger();

      make_repeating_event( p->sim,
        [ p, insight ] { return p->rng().gauss( insight->interval ); },  // gauss rng interval
        [ insight, p, uptime_pct ] {  // gauss rng uptime check
          if ( p->rng().roll( uptime_pct ) )
            insight->trigger();
          else
            insight->expire();
        } );
    } );
  }
}

// 1241711 driver
// 1230364 rppm driver
// 1230366 buff
void sunfire_silk_lining( special_effect_t& effect )
{
  auto stat_amount = effect.driver()->effectN( 1 ).average( effect );

  if ( auto buff = buff_t::find( effect.player, "radiant_acumen" ) )
  {
    // add stat from 2nd copy of embellishment
    debug_cast<stat_buff_t*>( buff )->add_stat_from_effect_type( A_MOD_STAT, stat_amount );
    return;
  }

  auto acumen = make_buff<stat_buff_t>( effect.player, "radiant_acumen", effect.trigger()->effectN( 1 ).trigger() )
    ->add_stat_from_effect_type( A_MOD_STAT, stat_amount );

  effect.custom_buff = acumen;
  effect.spell_id = effect.trigger()->id();

  new dbc_proc_callback_t( effect.player, effect );
}

// 1244238 driver
// 1259213 rppm driver
// 1259216 damage missile
// 1259218 damage
// 1259229 buff missile
// 1259230 buff
void devouring_banding( special_effect_t& effect )
{
  effect.player->sim->error( PLACEHOLDER,
    "devouring banding damage using effect driver value instead of rppm driver value" );
  effect.player->sim->error( PLACEHOLDER, "devouring banding buff not doubled with two copies" );

  auto proc_damage = effect.driver()->effectN( 1 ).average( effect );
  // auto proc_damage = effect.trigger()->effectN( 1 ).average( effect );

  if ( auto proc = effect.player->find_action( "devouring_bolt" ) )
  {
    // add damage from 2nd copy of embellishment
    proc->base_dd_min += proc_damage;
    proc->base_dd_max += proc_damage;
    return;
  }

  struct seized_power_t : public generic_proc_t
  {
    std::unordered_map<stat_e, buff_t*> buffs;

    seized_power_t( const special_effect_t& e ) : generic_proc_t( e, "seized_power", 1259229 )
    {
      quiet = true;

      // TODO: adjust buff value increase with two copies
      create_all_stat_buffs( e, data().effectN( 1 ).trigger(), 0, [ this ]( stat_e s, buff_t* b ) { buffs[ s ] = b; } );
    }

    void impact( action_state_t* ) override
    {
      auto stat = util::highest_stat( player, secondary_ratings );
      for ( auto [ s, b ] : buffs )
      {
        if ( s == stat )
          b->trigger();
        else
          b->expire();
      }
    }
  };

  // effect.trigger() == 1259213
  // ->effectN( 1 ).trigger() == 1259216
  // ->effectN( 1 ).trigger() == 1259218
  struct devouring_bolt_t : public generic_proc_t
  {
    devouring_bolt_t( const special_effect_t& e )
      : generic_proc_t( e, "devouring_bolt_missile", e.trigger()->effectN( 1 ).trigger() )
    {
      dual = true;

      impact_action = create_proc_action<generic_proc_t>( "devouring_bolt", e, data().effectN( 1 ).trigger() );
      stats = impact_action->stats;  // report the damage only
    }
  };

  auto damage = create_proc_action<devouring_bolt_t>( "devouring_bolt_missile", effect );
  damage->base_dd_min += proc_damage;
  damage->base_dd_max += proc_damage;
  auto buff = create_proc_action<seized_power_t>( "seized_power", effect );

  effect.spell_id = effect.trigger()->id();
  effect.player->callbacks.register_callback_execute_function( effect.spell_id, [ damage, buff ]( auto, auto, auto s ) {
    damage->execute_on_target( s->target );
    buff->execute_on_target( s->target );
  } );

  new dbc_proc_callback_t( effect.player, effect );
}

// 1244243 driver
// 1259060 coeff (real driver?)
// 1259061 buff
void blessed_pango_charm( special_effect_t& effect )
{
  effect.player->sim->error( PLACEHOLDER,
    "blessed pango charm buff using coeff spell value instead of effect driver value" );

  auto stat_amount = effect.player->find_spell( 1259060 )->effectN( 1 ).average( effect );

  if ( auto buff = buff_t::find( effect.player, "favored_by_kulzi" ) )
  {
    // add stat from 2nd copy of embellishment
    debug_cast<stat_buff_t*>( buff )->add_stat_from_effect_type( A_MOD_RATING, stat_amount );
    return;
  }

  auto favored = make_buff<stat_buff_t>( effect.player, "favored_by_kulzi", effect.trigger() )
    ->add_stat_from_effect_type( A_MOD_RATING, stat_amount );

  effect.custom_buff = favored;

  new dbc_proc_callback_t( effect.player, effect );
}

// 1244276 driver
// 1259124 rppm driver
// 1259127 heal coeff?
// 1259128 damage
// 1259130 heal
void primal_spore_binding( special_effect_t& effect )
{
  effect.player->sim->error( PLACEHOLDER,
    "primal spore binding damage and heal using effect driver value instead of rppm driver value" );

  auto damage_amount = effect.driver()->effectN( 1 ).average( effect );
  auto heal_amount = effect.driver()->effectN( 2 ).average( effect );

  if ( auto proc = effect.player->find_action( "primal_spore_explosion" ) )
  {
    // add damage from 2nd copy of embellishment
    proc->base_dd_min += damage_amount;
    proc->base_dd_max += damage_amount;

    auto heal = effect.player->find_action( "primal_spore_explosion_heal" );
    assert( heal );
    heal->base_dd_min += heal_amount;
    heal->base_dd_max += heal_amount;

    return;
  }

  auto damage = create_proc_action<generic_aoe_proc_t>( "primal_spore_explosion", effect, 1259128, true );
  damage->base_dd_min += damage_amount;
  damage->base_dd_max += damage_amount;

  auto heal =
    create_proc_action<base_generic_aoe_proc_t<proc_heal_t>>( "primal_spore_explosion_heal", effect, 1259130, true );
  heal->base_dd_min += heal_amount;
  heal->base_dd_max += heal_amount;
  heal->name_str_reporting = "Heal";

  effect.spell_id = effect.trigger()->id();
  effect.player->callbacks.register_callback_execute_function( effect.spell_id, [ damage, heal ]( auto, auto, auto s ) {
    if ( s->target->is_enemy() )
      damage->execute_on_target( s->target );
    else
      heal->execute_on_target( s->target );
  } );

  new dbc_proc_callback_t( effect.player, effect );
}

// 1251906 driver
// 1252383 rppm driver
// 1252389 dot
void prismatic_focusing_iris( special_effect_t& effect )
{
  effect.player->sim->error( PLACEHOLDER,
    "prismatic focusing iris damage using rppm driver value instead of effect driver value" );

  auto dot_damage = effect.trigger()->effectN( 2 ).average( effect );

  if ( auto proc = effect.player->find_action( "prismatic_focusing_iris" ) )
  {
    // add damage from 2nd copy of embellishment
    proc->base_td += dot_damage;
    return;
  }

  auto damage_spell = effect.trigger()->effectN( 1 ).trigger();
  auto pct_per_gem = effect.driver()->effectN( 3 ).percent();

  auto dot = create_proc_action<generic_proc_t>( "prismatic_focusing_iris", effect, damage_spell );
  dot->base_td += dot_damage;
  dot->base_td_multiplier *= 1.0 + ( pct_per_gem * unique_gem_list( effect.player, gem_colors ).size() );
  dot->base_td_multiplier *= role_mult( effect.player, damage_spell );
  dot->base_td_multiplier *= bandolier_mul( effect.player );

  effect.spell_id = effect.trigger()->id();
  effect.execute_action = dot;

  new dbc_proc_callback_t( effect.player, effect );
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
// 1244005 driver
// 1259297 coeff
// 1259504 shiv missile
// 1259264 shiv direct damage
// 1259503 crystal missile
// 1259273 crystal aoe damage
// 1259508 tonic missile
// 1259276 tonic heal
namespace
{
template <typename T>
action_t* create_mrm_action( std::string n, const special_effect_t& e, unsigned id, double a )
{
  auto missile = create_proc_action<generic_proc_t>( n + "_missile", e, id );
  auto impact = create_proc_action<T>( n, e, missile->data().effectN( 1 ).trigger() );
  impact->base_dd_min = impact->base_dd_max = a;
  missile->dual = true;
  missile->stats = impact->stats;  // report the damage only
  missile->impact_action = impact;

  return missile;
}
}  // namespace

void murder_row_materials( special_effect_t& effect )
{
  effect.player->sim->error( PLACEHOLDER, "murder row materials proc effect based on trigger action" );
  effect.player->sim->error( PLACEHOLDER, "murder row materials item level scaling unknown" );
  effect.player->sim->error( PLACEHOLDER, "murder row materials shiv assumed to use effect#1 coeff" );
  effect.player->sim->error( PLACEHOLDER, "murder row materials crystal assumed to use effect#2 coeff" );
  effect.player->sim->error( PLACEHOLDER, "murder row materials crystal assumed to split + increase per target " );
  effect.player->sim->error( PLACEHOLDER, "murder row materials tonic assumed to use effect#3 coeff" );

  auto shiv_amount = effect.trigger()->effectN( 1 ).average( effect );
  auto crystal_amount = effect.trigger()->effectN( 2 ).average( effect );
  auto tonic_amount = effect.trigger()->effectN( 3 ).average( effect );

  auto shiv =
    create_mrm_action<generic_proc_t>( "murder_row_shiv", effect, 1259504, shiv_amount );
  auto crystal =
    create_mrm_action<generic_aoe_proc_t>( "slightlystabilized_arcanocrystal", effect, 1259503, crystal_amount );
  auto tonic =
    create_mrm_action<generic_heal_t>( "emergency_healing_tonic", effect, 1259508, tonic_amount );

  effect.proc_flags2_ = PF2_CRIT;
  effect.player->callbacks.register_callback_execute_function( effect.spell_id,
    [ shiv, crystal, tonic ]( auto, auto, auto s ) {
      if ( !s->target->is_enemy() )
        tonic->execute_on_target( s->target );
      else if ( s->n_targets > 1 )
        crystal->execute_on_target( s->target );
      else
        shiv->execute_on_target( s->target );
    } );

  new dbc_proc_callback_t( effect.player, effect );
}

void root_wardens_regalia( special_effect_t& effect )
{

}

void torments_duality( special_effect_t& effect )
{

}
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
  register_special_effect( 1251906, embellishments::prismatic_focusing_iris );
  register_special_effect( 1251905, DISABLED_EFFECT );  // stabilizing gemstone bandolier
  // Trinkets
  // Weapons
  // Armor
  register_special_effect( 1244005, sets::murder_row_materials );
  register_special_effect( 1244021, sets::root_wardens_regalia );
  register_special_effect( 1253358, sets::torments_duality );
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

double bandolier_mul( player_t* p )
{
  if ( unique_gear::find_special_effect( p, 1251905 ) )
    return 2.0;  // hardcoded
  else
    return 1.0;
}
}  // namespace unique_gear::midnight
