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
// Food
static constexpr unsigned food_coeff_spell_id = 1219179;
using selector_fn = std::function<stat_e( const player_t*, util::span<const stat_e> )>;

struct selector_food_buff_t : public consumable_buff_t<stat_buff_t>
{

  double amount;
  bool highest;

  selector_food_buff_t( const special_effect_t& e, bool b )
    : consumable_buff_t( e.player, e.name(), e.driver() ), highest( b )
  {
    amount = e.stat_amount;
  }

  void start( int s, double v, timespan_t d ) override
  {
    auto stat = highest ? util::highest_stat( player, secondary_ratings )
                        : util::lowest_stat( player, secondary_ratings );

    if( !manual_stats_added )
      add_stat( stat, amount );

    consumable_buff_t::start( s, v, d );
  }
};

custom_cb_t selector_food( unsigned id, bool highest, bool major = true )
{
  return [ = ]( special_effect_t& effect ) {
    effect.spell_id = id;

    auto coeff = effect.player->find_spell( food_coeff_spell_id );

    effect.stat_amount = coeff->effectN( 4 ).average( effect );
    if ( !major )
      effect.stat_amount *= coeff->effectN( 1 ).base_value() * 0.1;

    effect.custom_buff = new selector_food_buff_t( effect, highest );
  };
}

custom_cb_t primary_food( unsigned id, stat_e stat, size_t primary_idx = 3, bool major = true )
{
  return [ = ]( special_effect_t& effect ) {
    effect.spell_id = id;

    auto coeff = effect.player->find_spell( food_coeff_spell_id );

    auto buff = create_buff<consumable_buff_t<stat_buff_t>>( effect.player, effect.driver() );

    if ( primary_idx )
    {
      auto _amt = coeff->effectN( primary_idx ).average( effect );
      if ( !major )
        _amt *= coeff->effectN( 1 ).base_value() * 0.1;

      buff->add_stat( effect.player->convert_hybrid_stat( stat ), _amt );
    }

    if ( primary_idx == 3 )
    {
      auto _amt = coeff->effectN( 8 ).average( effect );
      if ( !major )
        _amt *= coeff->effectN( 1 ).base_value() * 0.1;

      buff->add_stat( STAT_STAMINA, _amt );
    }

    effect.custom_buff = buff;
  };
}

custom_cb_t secondary_food( unsigned id, stat_e stat1, stat_e stat2 = STAT_NONE )
{
  return [ = ]( special_effect_t& effect ) {
    effect.spell_id = id;

    auto coeff = effect.player->find_spell( food_coeff_spell_id );

    auto buff = create_buff<consumable_buff_t<stat_buff_t>>( effect.player, effect.driver() );

    if ( stat2 == STAT_NONE )
    {
      auto _amt = coeff->effectN( 4 ).average( effect );
      buff->add_stat( stat1, _amt );
    }
    else
    {
      auto _amt = coeff->effectN( 5 ).average( effect );
      buff->add_stat( stat1, _amt );
      buff->add_stat( stat2, _amt );
    }

    effect.custom_buff = buff;
  };
}
// Potions
// Draught of Rampant Abandon
// 1236998 driver & buff
// 1237154 AoE trigger (NYI)
void draught_of_rampant_abandon( special_effect_t& effect )
{
  auto buff = buff_t::find( effect.player, "draught_of_rampant_abandon" );
  if ( !buff )
  {
    // TODO: RPPM is for the AoE trigger (NYI), disabling for now
    buff = make_buff<stat_buff_t>( effect.player, "draught_of_rampant_abandon", effect.driver() )
               ->add_stat_from_effect( 1, effect.driver()->effectN( 1 ).average( effect ) )
               ->set_rppm( rppm_scale_e::RPPM_DISABLE );
  }

  effect.custom_buff = buff;
}

// Potion of Recklessness
// 1236994 - driver & buff
void potion_of_recklessness( special_effect_t& effect )
{
  // The potion grants a positive buff to your highest secondary stat and
  // a negative buff to your lowest secondary stat.
  struct recklessness_buff_t : public consumable_buff_t<stat_buff_t>
  {
    double pos_crit = 0.0, pos_haste = 0.0, pos_vers = 0.0, pos_mast = 0.0;
    double neg_crit = 0.0, neg_haste = 0.0, neg_vers = 0.0, neg_mast = 0.0;

    recklessness_buff_t( const special_effect_t& e ) : consumable_buff_t( e.player, e.name(), e.driver() )
    {
      // Mapping from the driver effects
      pos_crit = e.driver()->effectN( 2 ).average( e );
      pos_haste = e.driver()->effectN( 3 ).average( e );
      pos_vers  = e.driver()->effectN( 4 ).average( e );
      pos_mast  = e.driver()->effectN( 5 ).average( e );

      neg_crit = e.driver()->effectN( 6 ).average( e );
      neg_haste = e.driver()->effectN( 7 ).average( e );
      neg_vers  = e.driver()->effectN( 8 ).average( e );
      neg_mast  = e.driver()->effectN( 9 ).average( e );

      set_constant_behavior( buff_constant_behavior::NEVER_CONSTANT );
    }

    void start( int s, double v, timespan_t d ) override
    {
      auto high = util::highest_stat( player, secondary_ratings );
      auto low = util::lowest_stat( player, secondary_ratings );

      if ( !manual_stats_added )
      {
        // apply positive to highest secondary
        switch ( high )
        {
          case STAT_CRIT_RATING: add_stat( STAT_CRIT_RATING, pos_crit ); break;
          case STAT_HASTE_RATING: add_stat( STAT_HASTE_RATING, pos_haste ); break;
          case STAT_VERSATILITY_RATING: add_stat( STAT_VERSATILITY_RATING, pos_vers ); break;
          case STAT_MASTERY_RATING: add_stat( STAT_MASTERY_RATING, pos_mast ); break;
          default: break;
        }

        // apply negative to lowest secondary
        switch ( low )
        {
          case STAT_CRIT_RATING: add_stat( STAT_CRIT_RATING, neg_crit ); break;
          case STAT_HASTE_RATING: add_stat( STAT_HASTE_RATING, neg_haste ); break;
          case STAT_VERSATILITY_RATING: add_stat( STAT_VERSATILITY_RATING, neg_vers ); break;
          case STAT_MASTERY_RATING: add_stat( STAT_MASTERY_RATING, neg_mast ); break;
          default: break;
        }
      }

      consumable_buff_t::start( s, v, d );
    }
  };

  auto buff = buff_t::find( effect.player, "potion_of_recklessness" );
  if ( !buff )
    buff = new recklessness_buff_t( effect );

  effect.custom_buff = buff;
}
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
    effect.player->register_precombat_begin( [ insight ]( player_t* ) { insight->trigger(); } );
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
// Heart of the Wind
// 1250599 Driver
// 1263318 Buff
void heart_of_the_wind( special_effect_t& effect )
{
  auto buff = create_buff<stat_buff_t>( effect.player, "the_wind_awoken", effect.driver()->effectN( 1 ).trigger() )
                  ->set_stat_from_effect_type( A_MOD_RATING, effect.driver()->effectN( 1 ).average( effect ) );

  effect.custom_buff = buff;

  new dbc_proc_callback_t( effect.player, effect );
}

// Kroluk's Warbanner
// 1250563 driver
// 1255816 damage
void kroluks_warbanner( special_effect_t& effect )
{
  auto damage = create_proc_action<generic_proc_t>( "kroluks_warbanner", effect, 1255816 );
  damage->base_dd_min = damage->base_dd_max = effect.driver()->effectN( 1 ).average( effect );
  damage->base_multiplier *= role_mult( effect );

  effect.execute_action = damage;

  new dbc_proc_callback_t( effect.player, effect );
}

// Vessel of Souls
// 1250602 Driver
// 1265513 Area Trigger
// 1265566 Buff
// TODO: RNG for missing pots of souls? They spawn witnin 5 yards of the player, qutie hard to miss.
void vessel_of_souls( special_effect_t& effect )
{
  auto buff = create_buff<stat_buff_t>( effect.player, "a_restless_soul", effect.player->find_spell( 1265566 ) )
                  ->set_stat_from_effect_type( A_MOD_STAT, effect.driver()->effectN( 1 ).average( effect ) )
                  ->set_stack_behavior( buff_stack_behavior::ASYNCHRONOUS );

  effect.custom_buff = buff;

  new dbc_proc_callback_t( effect.player, effect );
}

// Mark of Light
// 1250582 Driver
// 1258226 Damage
void mark_of_light( special_effect_t& effect )
{
  auto damage = create_proc_action<generic_aoe_proc_t>( "mark_of_light", effect, 1258226 );
  damage->base_dd_min = damage->base_dd_max = effect.driver()->effectN( 1 ).average( effect );
  damage->base_multiplier *= role_mult( effect );

  effect.execute_action = damage;

  new dbc_proc_callback_t( effect.player, effect );
}

// Solarflare Prism
// 1254640 Driver
// 1255504 Buff
void solarflare_prism( special_effect_t& effect )
{
  struct solarflare_prism_buff_t : public stat_buff_t
  {
    double hp_inc;
    double max_val;
    double hp_mult;

    solarflare_prism_buff_t( std::string_view n, const special_effect_t& e )
      : stat_buff_t( e.player, n, e.player->find_spell( 1255504 ) ), hp_inc( 0.0 ), max_val( 0.0 ), hp_mult( 0.0 )
    {
      set_default_value( e.driver()->effectN( 1 ).average( e ) );

      hp_inc  = e.driver()->effectN( 2 ).average( e );
      max_val = e.driver()->effectN( 4 ).average( e );
    }

    void bump( int s, double v ) override
    {
      for ( auto& s : stats )
      {
        s.amount = std::min( default_value + hp_inc * hp_mult, max_val );
      }
      stat_buff_t::bump( s, v );
    }
  };

  struct solarflare_prism_cb_t final : public dbc_proc_callback_t
  {
    buff_t* buff;

    solarflare_prism_cb_t( const special_effect_t& e ) : dbc_proc_callback_t( e.player, e ), buff( nullptr )
    {
      buff = make_buff<solarflare_prism_buff_t>( "solarflare_prism", e );
    }

    void execute( action_t*, action_state_t* s ) override
    {
      solarflare_prism_buff_t* sf_buff = debug_cast<solarflare_prism_buff_t*>( buff );
      sf_buff->hp_mult                 = 100 - s->target->health_percentage();
      sf_buff->trigger();
    }
  };

  new solarflare_prism_cb_t( effect );
}

}  // namespace trinkets

namespace weapons
{
// 1253357 umbral driver
// 1265822 umbral damage
// 1253359 radiant driver
// 1266012 radiant damage
// 1265823 void tear
// 1253358 set
void torments_duality( special_effect_t& effect )
{
  struct torments_duality_cb_t : public dbc_proc_callback_t
  {
    torments_duality_cb_t( const special_effect_t& e ) : dbc_proc_callback_t( e.player, e )
    {
      target_debuff = e.player->find_spell( 1265823 );
    }

    buff_t* create_debuff( player_t* t ) override
    {
      if ( auto debuff = buff_t::find( t, "void_tear", listener ) )
        return debuff;

      return make_buff( actor_pair_t( t, listener ), "void_tear", target_debuff );
    }
  };

  auto proxy = effect.player->find_action( "torments_duality" );
  if ( !proxy )
  {
    proxy = new action_t( action_e::ACTION_OTHER, "torments_duality", effect.player );
    proxy->name_str_reporting = "Torment's Duality";
  }

  if ( effect.spell_id == 1253357 )  // umbral sabre
  {
    auto damage = create_proc_action<generic_proc_t>( "umbral_sabre", effect, effect.trigger() );
    damage->base_dd_min = damage->base_dd_max = effect.driver()->effectN( 1 ).average( effect );
    proxy->add_child( damage );

    // two stacks at a time with set
    auto stacks = find_special_effect( effect.player, 1253358 ) ? 2 : 1;

    effect.player->callbacks.register_callback_execute_function( effect.spell_id,
      [ damage, stacks ]( auto cb, auto, auto s ) {
        damage->execute_on_target( s->target );
        cb->get_debuff( s->target )->trigger( stacks );
      } );
  }
  else if ( effect.driver()->id() == 1253359 )  // radiant foil
  {
    effect.player->sim->error( PLACEHOLDER, "radiant foil base_dd_adder increased per void tear stack" );

    auto damage = create_proc_action<generic_proc_t>( "radiant_foil", effect, effect.trigger() );
    damage->base_dd_min = damage->base_dd_max = effect.driver()->effectN( 1 ).average( effect );
    proxy->add_child( damage );

    // assume additional damage per stack is added to base damage
    auto void_add = effect.driver()->effectN( 2 ).average( effect );

    effect.player->callbacks.register_callback_execute_function( effect.spell_id,
      [ damage, void_add ]( auto cb, auto, auto s ) {
        auto _debuff = cb->get_debuff( s->target );

        damage->base_dd_adder = void_add * _debuff->check();
        damage->execute_on_target( s->target );
        damage->base_dd_adder = 0.0;

        _debuff->expire();
      } );
  }

  new torments_duality_cb_t( effect );
}
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

// 1244021 driver
// 1258885 vers
// 1258886 mast
// 1258887 haste
// 1258890 crit
void root_wardens_regalia( special_effect_t& effect )
{
  effect.player->sim->error( PLACEHOLDER, "root wardens regalia buffs are not mutually exclusive" );

  auto stat_amount = effect.driver()->effectN( 1 ).average( effect );

  std::vector<stat_buff_t*> buffs;

  for ( auto id : { 1258885, 1258886, 1258887, 1258890 } )
  {
    auto _b = create_buff<stat_buff_t>( effect.player, effect.player->find_spell( id ) )
      ->add_stat_from_effect_type( A_MOD_RATING, stat_amount );
    buffs.push_back( _b );
  }

  effect.player->callbacks.register_callback_execute_function( effect.spell_id, [ buffs ]( auto cb, auto, auto ) {
    cb->rng().range( buffs )->trigger();
  } );

  new dbc_proc_callback_t( effect.player, effect );
};
}

void register_special_effects()
{
  // NOTE: use unique_gear:: namespace for static consumables so we don't activate them with enable_all_item_effects
  // Food
  unique_gear::register_special_effect( 1232257, consumables::selector_food( 1219185, true ) );  // bloom skewers / [PH] Vegetarian Recipe
  unique_gear::register_special_effect( 1232916, consumables::selector_food( 1219185, true ) );  // braised blood hunter
  unique_gear::register_special_effect( 1232915, consumables::selector_food( 1219185, true ) );  // crimson calamari
  unique_gear::register_special_effect( 1219187, consumables::selector_food( 1219185, true ) );  // felberry figs
  unique_gear::register_special_effect( 1232914, consumables::selector_food( 1219185, true ) );  // tasty smoked tetra
  unique_gear::register_special_effect( 1232489, consumables::selector_food( 1219185, true ) );  // twilight angler's medley
  unique_gear::register_special_effect( 1259656, consumables::primary_food( 1232324, STAT_STR_AGI_INT, 2 ) ); // blooming feast
  unique_gear::register_special_effect( 1259657, consumables::primary_food( 1232325, STAT_STR_AGI_INT, 2 ) ); // quel'dorei medley
  unique_gear::register_special_effect( 1259658, consumables::primary_food( 1232582, STAT_STR_AGI_INT, 2 ) ); // rootland celebration
  unique_gear::register_special_effect( 1259659, consumables::primary_food( 1232585, STAT_STR_AGI_INT, 2 ) ); // silvermoon parade
  unique_gear::register_special_effect( 1232919, consumables::primary_food( 1233408, STAT_INTELLECT, 3 ) ); // flora frenzy / champion's bento
  unique_gear::register_special_effect( 1232917, consumables::primary_food( 1232584, STAT_STAMINA, 7 ) );  // royal roast
  unique_gear::register_special_effect( 1232902, consumables::secondary_food( 1219183, STAT_CRIT_RATING ) ); // arcano cutlets
  unique_gear::register_special_effect( 1232903, consumables::secondary_food( 1232087, STAT_HASTE_RATING ) ); // fel-kissed filet
  unique_gear::register_special_effect( 1232905, consumables::secondary_food( 1232089, STAT_MASTERY_RATING ) ); // warped wise wings
  unique_gear::register_special_effect( 1232906, consumables::secondary_food( 1232091, STAT_VERSATILITY_RATING ) ); // void-kissed fish rolls
  unique_gear::register_special_effect( 1232253, consumables::secondary_food( 1232321, STAT_CRIT_RATING, STAT_VERSATILITY_RATING ) ); // spiced biscuits
  unique_gear::register_special_effect( 1232910, consumables::secondary_food( 1232492, STAT_VERSATILITY_RATING, STAT_SPEED_RATING ) ); // buttered root crab
  unique_gear::register_special_effect( 1232481, consumables::secondary_food( 1233400, STAT_MASTERY_RATING, STAT_HASTE_RATING ) ); // bloodthistle-wrapped cutlets
  unique_gear::register_special_effect( 1232485, consumables::secondary_food( 1232318, STAT_MASTERY_RATING, STAT_CRIT_RATING ) ); // eversong pudding
  unique_gear::register_special_effect( 1232246, consumables::secondary_food( 1232316, STAT_MASTERY_RATING, STAT_HASTE_RATING ) ); // farstrider rations
  unique_gear::register_special_effect( 1232252, consumables::secondary_food( 1233403, STAT_MASTERY_RATING, STAT_VERSATILITY_RATING ) ); // silvermoon standard
  unique_gear::register_special_effect( 1232908, consumables::secondary_food( 1232491, STAT_MASTERY_RATING, STAT_SPEED_RATING ) ); // null and void plate
  unique_gear::register_special_effect( 1232483, consumables::secondary_food( 1233401, STAT_MASTERY_RATING, STAT_SPEED_RATING ) ); // hearthflame supper
  unique_gear::register_special_effect( 1232251, consumables::secondary_food( 1233404, STAT_MASTERY_RATING, STAT_CRIT_RATING ) ); // forager's medley
  unique_gear::register_special_effect( 1232487, consumables::secondary_food( 1233402, STAT_CRIT_RATING, STAT_VERSATILITY_RATING ) ); // wise tails
  unique_gear::register_special_effect( 1232486, consumables::secondary_food( 1232318, STAT_CRIT_RATING, STAT_VERSATILITY_RATING ) ); // fried bloomtail
  unique_gear::register_special_effect( 1232909, consumables::secondary_food( 1232493, STAT_HASTE_RATING, STAT_SPEED_RATING ) ); // glitter skewers
  unique_gear::register_special_effect( 1232250, consumables::secondary_food( 1233405, STAT_VERSATILITY_RATING, STAT_HASTE_RATING ) ); // quick sandwich
  unique_gear::register_special_effect( 1232907, consumables::secondary_food( 1232490, STAT_CRIT_RATING, STAT_SPEED_RATING ) ); // sun-seared lumifin
  unique_gear::register_special_effect( 1232249, consumables::secondary_food( 1233401, STAT_CRIT_RATING, STAT_HASTE_RATING ) ); // portable snack
  unique_gear::register_special_effect( 1232484, consumables::secondary_food( 1233405, STAT_VERSATILITY_RATING, STAT_HASTE_RATING ) ); // sunwell delight
  // Flasks
  // Potions
  unique_gear::register_special_effect( 1236998, consumables::draught_of_rampant_abandon );
  unique_gear::register_special_effect( 1236994, consumables::potion_of_recklessness );
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
  register_special_effect( 1250599, trinkets::heart_of_the_wind );
  register_special_effect( 1250563, trinkets::kroluks_warbanner );
  register_special_effect( 1250602, trinkets::vessel_of_souls );
  register_special_effect( 1250582, trinkets::mark_of_light );
  register_special_effect( 1254640, trinkets::solarflare_prism );
  // Weapons
  register_special_effect( { 1253357, 1253359 }, weapons::torments_duality );  // umbral sabre & radiant foil
  // Armor
  register_special_effect( 1244005, sets::murder_row_materials );
  register_special_effect( 1244021, sets::root_wardens_regalia );
  register_special_effect( 1253358, DISABLED_EFFECT );  // torments duality
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
