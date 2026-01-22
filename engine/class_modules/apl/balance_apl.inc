action_priority_list_t* precombat = get_action_priority_list( "precombat" );
action_priority_list_t* def = get_action_priority_list( "default" );
action_priority_list_t* st = get_action_priority_list( "st" );

precombat->add_action( "moonkin_form" );
precombat->add_action( "wrath" );
precombat->add_action( "wrath" );
precombat->add_action( "starfire" );

def->add_action( "variable,name=passive_asp,value=6%spell_haste+talent.natures_balance+((talent.bounteous_bloom&buff.bounteous_bloom.up)*15)+talent.orbit_breaker*dot.moonfire.ticking*(buff.orbit_breaker.stack>(27-2*buff.solstice.up))*24" );
def->add_action( "run_action_list,name=st" );

st->add_action( "sunfire,target_if=remains<3|refreshable" );
st->add_action( "moonfire,target_if=remains<3&refreshable" );
st->add_action( "fury_of_elune,if=5+variable.passive_asp<astral_power.deficit" );
st->add_action( "celestial_alignment,if=!buff.ca_inc.up" );
st->add_action( "incarnation,if=!buff.ca_inc.up" );
st->add_action( "use_item,slot=trinket1,if=buff.ca_inc.up" );
st->add_action( "use_item,slot=trinket2,if=buff.ca_inc.up" );
st->add_action( "potion,if=buff.ca_inc.up" );
st->add_action( "force_of_nature,if=buff.ca_inc.up|cooldown.ca_inc.remains>15" );
st->add_action( "starfall,if=buff.starweavers_warp.up" );
st->add_action( "starsurge,if=talent.starlord&buff.starlord.stack<3" );
st->add_action( "sunfire,target_if=refreshable" );
st->add_action( "moonfire,target_if=refreshable&(!talent.treants_of_the_moon|cooldown.force_of_nature.remains>3&!buff.harmony_of_the_grove.up)" );
st->add_action( "convoke_the_spirits,if=buff.ca_inc.up" );
st->add_action( "new_moon,if=astral_power.deficit>variable.passive_asp+energize_amount|fight_remains<20|cooldown.ca_inc.remains>15" );
st->add_action( "half_moon,if=astral_power.deficit>variable.passive_asp+energize_amount&(buff.eclipse_lunar.remains>execute_time|buff.eclipse_solar.remains>execute_time)|fight_remains<20|cooldown.ca_inc.remains>15" );
st->add_action( "full_moon,if=astral_power.deficit>variable.passive_asp+energize_amount&(buff.eclipse_lunar.remains>execute_time|buff.eclipse_solar.remains>execute_time)|fight_remains<20|cooldown.ca_inc.remains>15" );
st->add_action( "starsurge,if=buff.starweavers_weft.up|buff.touch_the_cosmos.up" );
st->add_action( "starsurge,if=astral_power.deficit<variable.passive_asp+action.wrath.energize_amount+(action.starfire.energize_amount+variable.passive_asp)*(buff.eclipse_solar.remains<(gcd.max+action.starfire.cast_time))" );
st->add_action( "force_of_nature,if=!hero_tree.keeper_of_the_grove" );
st->add_action( "wild_mushroom,if=!prev_gcd.1.wild_mushroom&dot.fungal_growth.remains<2" );
st->add_action( "starfire,if=talent.lunar_calling" );
st->add_action( "wrath" );
