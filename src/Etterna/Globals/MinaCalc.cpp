#include "MinaCalc.h"
#include <cmath>
#include <iostream>
#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <utility>
#include <cassert>

#include "MinaCalc/Ulbu.h"
#include "MinaCalcHelpers.h"

using std::max;
using std::min;
using std::pow;
using std::vector;

static const std::array<std::pair<unsigned, string_view>, 16> note_mapping = {
	{ { 0U, "----" },
	  { 1U, "x---" },
	  { 2U, "-x--" },
	  { 3U, "xx--" },
	  { 4U, "--x-" },
	  { 5U, "x-x-" },
	  { 6U, "-xx-" },
	  { 7U, "xxx-" },
	  { 8U, "---x" },
	  { 9U, "x--x" },
	  { 10U, "-x-x" },
	  { 11U, "xx-x" },
	  { 12U, "--xx" },
	  { 13U, "x-xx" },
	  { 14U, "-xxx" },
	  { 15U, "xxxx" } }
};

/* Note: if we want max control over stamina we need to have one model for
 * affecting the other skillsets to a certain degree, enough to push up longer
 * stream ratings into contention with shorter ones, and another for both a more
 * granular and influential modifier to calculate the end stamina rating with so
 * todo on that */

// Stamina Model params
static const float stam_ceil = 1.075234F; // stamina multiplier max
static const float stam_mag = 243.F;	  // multiplier generation scaler
static const float stam_fscale = 500.F; // how fast the floor rises (it's lava)
static const float stam_prop =
  0.69424F; // proportion of player difficulty at which stamina tax begins

static inline auto
TotalMaxPoints(const Calc& calc) -> int
{
	int MaxPoints = 0;
	for (int i = 0; i < calc.numitv; i++) {
		MaxPoints +=
		  calc.itv_points[left_hand].at(i) + calc.itv_points[right_hand].at(i);
	}
	return MaxPoints;
}

auto
Calc::CalcMain(const vector<NoteInfo>& NoteInfo,
			   float music_rate,
			   float score_goal) -> vector<float>
{
	// in flux
	float grindscaler =
	  CalcClamp(
		0.9F + (0.1F * ((NoteInfo.back().rowTime / music_rate) - 35.F) / 35.F),
		0.9F,
		1.F) *
	  CalcClamp(
		0.9F + (0.1F * ((NoteInfo.back().rowTime / music_rate) - 15.F) / 15.F),
		0.9F,
		1.F) *
	  CalcClamp(
		0.4F + (0.6F * ((NoteInfo.back().rowTime / music_rate) - 10.F) / 10.F),
		0.4F,
		1.F);

	// for multi offset passes
	// const int fo_rizzy = ssr ? 3 : 1;
	const int fo_rizzy = 1;
	vector<vector<float>> the_hizzle_dizzles(fo_rizzy);
	for (int WHAT_IS_EVEN_HAPPEN_THE_BOMB = 0;
		 WHAT_IS_EVEN_HAPPEN_THE_BOMB < fo_rizzy;
		 ++WHAT_IS_EVEN_HAPPEN_THE_BOMB) {

		bool continue_calc = InitializeHands(
		  NoteInfo,
		  music_rate,
		  0.1F * static_cast<float>(WHAT_IS_EVEN_HAPPEN_THE_BOMB));

		// if we exceed max_rows_for_single_interval during processing
		if (!continue_calc) {
			std::cout << "skipping junk file" << std::endl;
			return dimples_the_all_zero_output;
		}

		MaxPoints = TotalMaxPoints(*this);

		vector<float> mcbloop(NUM_Skillset);
		// overall and stam will be left as 0.f by this loop
		for (int i = 0; i < NUM_Skillset; ++i) {
			mcbloop[i] = Chisel(0.1F, 10.24F, score_goal, i, false);
		}

		// stam is based on which calc produced the highest
		// output without it
		int highest_base_skillset = max_index(mcbloop);
		float base = mcbloop[highest_base_skillset];

		/* rerun all with stam on, optimize by starting at the non-stam adjusted
		 * base value for each skillset we can actually set the stam floor to <
		 * 1 to shift the curve a bit do we actually need to rerun _all_ with
		 * stam on? we gain significant speed from not doing so, however the
		 * tradeoff is files that are close in 2/3 skillsets will have the stam
		 * bonus stripped from the second and third components, devaluing the
		 * file as a whole, we could run it for the 2nd/3rd highest skillsets
		 * but i'm too lazy to implement that right now */
		for (int i = 0; i < NUM_Skillset; ++i) {
			mcbloop[i] = Chisel(mcbloop[i] * 0.9F, 0.32F, score_goal, i, true);
		}

		int highest_stam_adjusted_skillset = max_index(mcbloop);

		/* all relative scaling to specific skillsets should occur before this
		 * point, not after (it ended up this way due to the normalizers which
		 * were dumb and removed) stam is the only skillset that can/should be
		 * normalized to base values without interfering with anything else
		 * (since it's not based on a type of pattern) */

		/* stam jams, stamina should push up the base ratings for files so files
		 * that are more difficult by virtue of being twice as long for more or
		 * less the same patterns don't get underrated, however they shouldn't
		 * be pushed up a huge amount either, we want high stream scores to be
		 * equally achieveable on longer or shorter files, ideally, the stam
		 * ratings itself is a separate consideration and will be scaled to the
		 * degree to which the stamina model affects the base rating, so while
		 * stamina should affect the base skillset ratings slightly we want the
		 * degree to which it makes files harder to be catalogued as the stamina
		 * rating scaling down stuff that has no stamina component will help
		 * preventing pollution of stamina leaderboards with charts that are
		 * just very high rated but take no stamina */
		float poodle_in_a_porta_potty = mcbloop[highest_base_skillset];

		/* the bigger this number the more stamina has to influence a file
		 * before it counts in the stam skillset, i.e. something that only
		 * benefits 2% from the stam modifiers will drop below the 1.0 mark and
		 * move closer to 0 with the pow, resulting in a very low stamina rating
		 * (we want this), something that benefits 5.5% will have the 0.5%
		 * overflow multiplied and begin gaining some stam, and something that
		 * benefits 15% will max out the possible stam rating, which is
		 * (currently) a 1.07 multiplier to the base maybe using a multiplier
		 * and not a difference would be better? */
		static const float stam_curve_shift = 0.015F;
		// ends up being a multiplier between ~0.8 and ~1
		float mcfroggerbopper =
		  pow((poodle_in_a_porta_potty / base) - stam_curve_shift, 2.5F);

		/* we wanted to shift the curve down a lot before pow'ing but it was too
		 * much to balance out, so we need to give some back, this is roughly
		 * equivalent of multiplying by 1.05 but also not really because math we
		 * don't want to push up the high end stuff anymore so just add to let
		 * stuff down the curve catch up a little remember we're operating on a
		 * multiplier */
		mcfroggerbopper = CalcClamp(mcfroggerbopper, 0.8F, 1.08F);
		mcbloop[Skill_Stamina] = poodle_in_a_porta_potty * mcfroggerbopper *
								 basescalers[Skill_Stamina];

		// sets the 'proper' debug output, doesn't (shouldn't) affect actual
		// values this is the only time debugoutput arg should be set to true
		if (debugmode) {
			Chisel(mcbloop[highest_stam_adjusted_skillset] - 0.16F,
				   0.32F,
				   score_goal,
				   highest_stam_adjusted_skillset,
				   true,
				   true);
		}

		/* the final push down, cap ssrs (score specific ratings) to stop vibro
		 * garbage and calc abuse from polluting leaderboards too much, a "true"
		 * 38 is still unachieved so a cap of 40 [sic] is _extremely_ generous
		 * do this for SCORES only, not cached file difficulties */
		if (ssr) {
			static const float ssrcap = 40.F;
			for (auto& r : mcbloop) {
				// so 50%s on 60s don't give 35s
				r = downscale_low_accuracy_scores(r, score_goal);
				/*if (highest_stam_adjusted_skillset == Skill_JackSpeed &&
					score_goal < 0.8F) {
					r = 0.F;
				}*/
				r = CalcClamp(r, r, ssrcap);
			}
		}

		// finished all modifications to skillset values, set overall
		mcbloop[Skill_Overall] = max_val(mcbloop);

		for (float bagles : mcbloop) {
			the_hizzle_dizzles[WHAT_IS_EVEN_HAPPEN_THE_BOMB].push_back(bagles);
		}
	}
	vector<float> yo_momma(NUM_Skillset);
	for (int farts = 0; farts < the_hizzle_dizzles[0].size(); ++farts) {
		vector<float> girls;
		girls.reserve(the_hizzle_dizzles.size());
		for (auto& the_hizzle_dizzle : the_hizzle_dizzles) {
			girls.push_back(the_hizzle_dizzle[farts]);
		}
		yo_momma[farts] = mean(girls) * grindscaler;
		girls.clear();
	}

	return yo_momma;
}

/*	The stamina model works by asserting a minimum difficulty relative to
	the supplied player skill level for which the player's stamina begins to
	wane. Experience in both gameplay and algorithm testing has shown the
	appropriate value to be around 0.8. The multiplier is scaled to the
	proportionate difference in player skill. */

inline void
StamAdjust(float x, int ss, Calc& calc, int hi, bool debug = false)
{
	float stam_floor =
	  0.95F;		   // stamina multiplier min (increases as chart advances)
	float mod = 0.95F; // mutliplier

	float avs1 = 0.F;
	float avs2 = 0.F;
	float local_ceil = stam_ceil;
	const float super_stam_ceil = 1.11F;

	// use this to calculate the mod growth
	const std::array<float, max_intervals>* base_diff =
	  &(calc.base_diff_for_stam_mod.at(hi).at(ss));
	// but apply the mod growth to these values
	// they might be the same, or not
	const std::array<float, max_intervals>* diff =
	  &(calc.base_adj_diff.at(hi).at(ss));

	// i don't like the copypasta either but the boolchecks where
	// they were were too slow
	if (debug) {
		for (int i = 0; i < calc.numitv; i++) {
			avs1 = avs2;
			avs2 = base_diff->at(i);
			mod += ((((avs1 + avs2) / 2.F) / (stam_prop * x)) - 1.F) / stam_mag;
			if (mod > 0.95F) {
				stam_floor += (mod - 0.95F) / stam_fscale;
			}
			local_ceil = stam_ceil * stam_floor;

			mod = min(CalcClamp(mod, stam_floor, local_ceil), super_stam_ceil);
			calc.stam_adj_diff.at(i) = diff->at(i) * mod;
			calc.debugValues.at(hi)[2][StamMod][i] = mod;
		}
	} else {
		for (int i = 0; i < calc.numitv; i++) {
			avs1 = avs2;
			avs2 = base_diff->at(i);
			mod += ((((avs1 + avs2) / 2.F) / (stam_prop * x)) - 1.F) / stam_mag;
			if (mod > 0.95F) {
				stam_floor += (mod - 0.95F) / stam_fscale;
			}
			local_ceil = stam_ceil * stam_floor;

			mod = min(CalcClamp(mod, stam_floor, local_ceil), super_stam_ceil);
			calc.stam_adj_diff.at(i) = diff->at(i) * mod;
		}
	}
}

static const float magic_num = 4.F;
static const float magic_num_TWO = 2.5F;
static const float gratuitously_defined_zero_value = 0.F;

[[nodiscard]] inline auto
hit_the_road(const float& x, const float& y) -> float
{
	return magic_num - (magic_num * fastpow(x / y, magic_num_TWO));
}

/* ok this is a little jank, we are calculating jack loss looping over the
 * interval and interval size loop that adj_ni uses, or each rows per interval,
 * except we're doing it for each hand. aggregating jack diff into intervals
 * proves too problematic, so we'll calculate it by discrete notes, calculate
 * the point loss the same way, and then dump them into intervals just for calc
 * debug display. this also lets us put back some sort of jack stam model.
 * this sets pointloss debug values only, not diff. Note: jackloss should always
 * be a positive value, and be substracted */
inline auto
jackloss(const float& x, Calc& calc, const int& hi) -> float
{
	float total = 0.F;
	float row_loss = 0.F;

	// interval loop
	for (int itv = 0; itv < calc.numitv; ++itv) {
		float itv_total = 0.F;

		// rows per interval now
		for (int row = 0; row < calc.itv_jack_diff_size.at(hi).at(itv); ++row) {
			const auto& y = calc.jack_diff.at(hi).at(itv).at(row);

			if (x < y && y > 0.F) {
				row_loss = hit_the_road(x, y);
				itv_total += row_loss > 0.F ? row_loss : 0.F;
			}
		}

		// this is per _interval_, but calculated by row scanning
		calc.jack_loss.at(hi).at(itv) = itv_total;
		total += itv_total;
	}
	return total;
}

// we want intervals not row values here, just average them, it's only for calc
// display and doesn't affect internal calculations
inline void
set_jack_diff_debug(Calc& calc, const int& hi)
{
	// interval loop
	for (int itv = 0; itv < calc.numitv; ++itv) {
		float diff_total = 0.F;
		int counter = 0;

		// rows per interval now
		for (int row = 0; row < calc.itv_jack_diff_size.at(hi).at(itv); ++row) {
			diff_total += calc.jack_diff.at(hi).at(itv).at(row);
			++counter;
		}

		// technically this is kind of a waste of an array but whatever
		calc.soap.at(hi)[JackBase].at(itv) =
		  counter > 0 ? diff_total / static_cast<float>(counter) : 0.F;
	}
}

// debug bool here is NOT the one in Calc, it is passed from chisel
// using the final difficulty as the starting point and should only
// be executed once per chisel
void
CalcInternal(float& gotpoints,
			 float& x,
			 int ss,
			 bool stam,
			 Calc& calc,
			 int hi,
			 bool debug = false)
{
	if (stam) {
		StamAdjust(x, ss, calc, hi);
	}

	// final difficulty values to use
	const std::array<float, max_intervals>* v =
	  &(stam ? calc.stam_adj_diff : calc.base_adj_diff.at(hi).at(ss));
	float powindromemordniwop = 1.7F;
	if (ss == Skill_Chordjack) {
		powindromemordniwop = 1.7F;
	}

	// i don't like the copypasta either but the boolchecks where
	// they were were too slow
	if (debug) {
		calc.debugValues.at(hi)[2][StamMod].resize(calc.numitv);
		calc.debugValues.at(hi)[2][Pts].resize(calc.numitv);
		calc.debugValues.at(hi)[2][PtLoss].resize(calc.numitv);
		calc.debugValues.at(hi)[2][JackPtLoss].resize(calc.numitv);
		calc.debugValues.at(hi)[1][MSD].resize(calc.numitv);
		// final debug output should always be with stam activated
		StamAdjust(x, ss, calc, hi, true);
		for (int i = 0; i < calc.numitv; ++i) {
			calc.debugValues.at(hi)[1][MSD].at(i) = (*v).at(i);
		}

		for (int i = 0; i < calc.numitv; ++i) {
			auto pts = static_cast<float>(calc.itv_points.at(hi).at(i));
			calc.debugValues.at(hi)[2][Pts].at(i) = pts;
			if (x < (*v).at(i)) {
				float lostpoints =
				  (pts - (pts * fastpow(x / (*v).at(i), powindromemordniwop)));
				gotpoints -= lostpoints;
				calc.debugValues.at(hi)[2][PtLoss].at(i) = abs(lostpoints);
			}
		}
	} else {
		for (int i = 0; i < calc.numitv; ++i) {
			if (x < (*v).at(i)) {
				auto pts = static_cast<float>(calc.itv_points.at(hi).at(i));
				gotpoints -=
				  (pts - (pts * fastpow(x / (*v).at(i), powindromemordniwop)));
			}
		}
	}
}

auto
Calc::InitializeHands(const vector<NoteInfo>& NoteInfo,
					  float music_rate,
					  float offset) -> bool
{
	fastwalk(NoteInfo, music_rate, *this, offset);

	TheGreatBazoinkazoinkInTheSky ulbu_that_which_consumes_all(*this);
	ulbu_that_which_consumes_all();

	// main hand loop
	for (auto& hi : { left_hand, right_hand }) {
		InitAdjDiff(*this, hi);

		// post pattern mod smoothing for cj
		Smooth(base_adj_diff.at(hi).at(Skill_Chordjack), 1.F, numitv);
	}

	// debug info loop
	if (debugmode) {
		for (auto& hi : { left_hand, right_hand }) {
			// pattern mods and base msd never change, set degbug
			// output for them now

			// 3 = number of different debug types
			debugValues.at(hi).resize(3);
			debugValues.at(hi)[0].resize(NUM_CalcPatternMod);
			debugValues.at(hi)[1].resize(NUM_CalcDiffValue);
			debugValues.at(hi)[2].resize(NUM_CalcDebugMisc);

			// pattern mods first
			for (int pmod = 0; pmod < NUM_CalcPatternMod; ++pmod) {
				debugValues.at(hi)[0][pmod].resize(numitv);

				for (int itv = 0; itv < numitv; ++itv) {
					debugValues.at(hi)[0][pmod][itv] =
					  doot.at(hi).at(pmod).at(itv);
				}
			}

			// set the base diffs - everything but final adjusted values
			for (int diff = 0; diff < NUM_CalcDiffValue - 1; ++diff) {
				debugValues.at(hi)[1][diff].resize(numitv);

				// these are calculated by row, so we need to aggregate them
				// into intervals before setting the values
				if (diff == JackBase) {
					set_jack_diff_debug(*this, hi);
				}

				for (int itv = 0; itv < numitv; ++itv) {
					debugValues.at(hi)[1][diff][itv] =
					  soap.at(hi).at(diff).at(itv);
				}
			}
		}
	}
	return true;
}

// each skillset should just be a separate calc function [todo]
auto
Calc::Chisel(float player_skill,
			 float resolution,
			 float score_goal,
			 int ss,
			 bool stamina,
			 bool debugoutput) -> float
{

	float gotpoints = 0.F;
	float reqpoints = static_cast<float>(MaxPoints) * score_goal;
	for (int iter = 1; iter <= 8; iter++) {
		do {
			if (player_skill > 100.F) {
				return 0.F;
			}
			player_skill += resolution;
			if (ss == Skill_Overall || ss == Skill_Stamina) {
				return 0.F; // not how we set these values
			}

			// reset tallied score, always deduct rather than accumulate now
			if (ss == Skill_JackSpeed || ss == Skill_Technical) {
				gotpoints = static_cast<float>(MaxPoints);
			} else {
				// waters down scaling on generic skillsets
				gotpoints = static_cast<float>(MaxPoints) * 1.05F;
			}

			for (auto& hi : { left_hand, right_hand }) {

				/* only run the other hand if we're still above the
				 * reqpoints, if we're already below, there's no
				 * point, i.e. we're so far below the skill
				 * benchmark it's impossible to reach the goal after
				 * just the first hand's losses are totaled */
				if (gotpoints > reqpoints) {
					if (ss == Skill_JackSpeed) {
						gotpoints -= jackloss(player_skill, *this, hi);
					} else {
						CalcInternal(
						  gotpoints, player_skill, ss, stamina, *this, hi);
					}
					if (ss == Skill_Technical) {
						gotpoints -= fastsqrt(
						  jackloss(player_skill * 0.6F, *this, hi) * 0.75F);
					}
				}
			}
		} while (gotpoints < reqpoints);
		player_skill -= resolution;
		resolution /= 2.F;
	}

	/* these are the values for msd/stam adjusted msd/pointloss the
	 * latter two are dependent on player_skill and so should only
	 * be recalculated with the final value already determined for
	 * clarification, player_skill value being passed into here is
	 * the final value we've determined */

	if (debugoutput) {
		for (auto& hi : { left_hand, right_hand }) {
			CalcInternal(
			  gotpoints, player_skill, ss, stamina, *this, hi, debugoutput);

			for (int i = 0; i < numitv; ++i) {
				debugValues.at(hi)[2][JackPtLoss].at(i) =
				  jack_loss.at(hi).at(i);
			}

			/* set total pattern mod value (excluding stam for now), essentially
			 * this value is the cumulative effect of pattern mods on base nps
			 * for everything but tech, and base tech for tech */

			// techbase
			if (ss == Skill_Technical) {
				for (int i = 0; i < numitv; ++i) {
					debugValues.at(hi)[0][TotalPatternMod].at(i) =
					  base_adj_diff.at(hi)[TechBase].at(i) /
					  soap.at(hi)[TechBase].at(i);
				}
			} else if (ss == Skill_JackSpeed) {
				// no pattern mods atm, do nothing
			} else {
				// everything else uses nps base
				for (int i = 0; i < numitv; ++i) {
					debugValues.at(hi)[0][TotalPatternMod].at(i) =
					  base_adj_diff.at(hi).at(ss).at(i) /
					  soap.at(hi)[NPSBase].at(i);
				}
			}
		}
	}

	return player_skill + 2.F * resolution;
}

inline void
Calc::InitAdjDiff(Calc& calc, const int& hi)
{
	/* the new way we wil attempt to diffrentiate skillsets rather than using
	 * normalizers is by detecting whether or not we think a file is mostly
	 * comprised of a given pattern, producing a downscaler that slightly buffs
	 * up those files and produces a downscaler for files not detected of that
	 * type. the major potential failing of this system is that it ends up such
	 * that the rating is tied directly to whether or not a file can be more or
	 * less strongly determined to be of a pattern type, e.g. splithand trills
	 * being marked as more "js" than actual js, for the moment these modifiers
	 * are still built on proportion of taps in chords / total taps, but there's
	 * a lot more give than their used to be. they should be re-done as
	 * sequential detection for best effect but i don't know if that will be
	 * necessary for basic tuning if we don't do this files may end up
	 * misclassing hard and polluting leaderboards, and good scores on overrated
	 * files will simply produce high ratings in every category */

	static const std::array<vector<int>, NUM_Skillset> pmods_used = { {
	  // overall, nothing, don't handle here
	  {},

	  // stream
	  {
		Stream,
		OHTrill,
		VOHTrill,
		// Roll,
		Chaos,
		WideRangeRoll,
		WideRangeJumptrill,
		FlamJam,
		OHJumpMod,
		Balance,
		RanMan,
		WideRangeBalance,
	  },

	  // js
	  {
		JS,
		OHJumpMod,
		Chaos,
		Balance,
		TheThing,
		TheThing2,
		WideRangeBalance,
		WideRangeJumptrill,
		WideRangeRoll,
		OHTrill,
		VOHTrill,
		RanMan,
		// Roll,
		// WideRangeAnchor,
	  },

	  // hs
	  {
		HS,
		OHJumpMod,
		TheThing,
		WideRangeAnchor,
		WideRangeRoll,
		OHTrill,
		VOHTrill,
		// Roll
		RanMan,
	  },

	  // stam, nothing, don't handle here
	  {},

	  // jackspeed
	  {},

	  // chordjack
	  {
		CJ,
		CJDensity,
		// CJOHJump // SQRTD BELOW
		VOHTrill,
		WideRangeAnchor,
	  },

	  // tech, duNNO wat im DOIN
	  {
		OHTrill,
		VOHTrill,
		Balance,
		// Roll,
		OHJumpMod,
		Chaos,
		WideRangeJumptrill,
		WideRangeBalance,
		WideRangeRoll,
		FlamJam,
		RanMan,
		// WideRangeAnchor,
		TheThing,
		TheThing2,
	  },
	} };

	std::array<float, NUM_Skillset> tp_mods = {};

	// ok this loop is pretty wack i know, for each interval
	for (int i = 0; i < calc.numitv; ++i) {
		tp_mods.fill(1.F);

		// total pattern mods for each skillset, we want this to be
		// calculated before the main skillset loop because we might
		// want access to the total js mod while on stream, or
		// something
		for (int ss = 0; ss < NUM_Skillset; ++ss) {
			// is this even faster than multiplying 1.f by 1.f a
			// billion times?
			if (ss == Skill_Overall || ss == Skill_Stamina) {
				continue;
			}
			for (auto& pmod : pmods_used.at(ss)) {
				tp_mods.at(ss) *= calc.doot.at(hi).at(pmod).at(i);
			}
		}

		// main skillset loop, for each skillset that isn't overall
		// or stam
		for (int ss = 0; ss < NUM_Skillset; ++ss) {
			if (ss == Skill_Overall || ss == Skill_Stamina) {
				continue;
			}

			// this should work and not be super slow?
			float* adj_diff = &(calc.base_adj_diff.at(hi).at(ss).at(i));
			float* stam_base =
			  &(calc.base_diff_for_stam_mod.at(hi).at(ss).at(i));

			// might need optimization, or not since this is not
			// outside of a dumb loop now and is done once instead
			// of a few hundred times
			float funk = calc.soap.at(hi).at(NPSBase).at(i) * tp_mods.at(ss) *
						 basescalers.at(ss);
			*adj_diff = funk;
			*stam_base = funk;
			switch (ss) {
				// do funky special case stuff here
				case Skill_Stream:
					break;

				// test calculating stam for js/hs on max js/hs diff
				// we want hs to count against js so they are
				// mutually exclusive
				case Skill_Jumpstream: {

					*adj_diff /= max<float>(calc.doot.at(hi).at(HS).at(i), 1.F);
					*adj_diff /=
					  fastsqrt(calc.doot.at(hi).at(OHJumpMod).at(i) * 0.95F);

					/*adj_diff *=
					  CalcClamp(fastsqrt(doot.at(hi).at(RanMan).at(i)
					  - 0.2f), 1.f, 1.05f);*/
					// maybe we should have 2 loops to avoid doing
					// math twice
					float a = *adj_diff;
					float b = calc.soap.at(hi).at(NPSBase).at(i) *
							  tp_mods[Skill_Handstream];
					*stam_base = max<float>(a, b);
				} break;
				case Skill_Handstream: {

					// adj_diff /=
					// fastsqrt(doot.at(hi).at(OHJump).at(i));
					float a = funk;
					float b = calc.soap.at(hi).at(NPSBase).at(i) *
							  tp_mods[Skill_Jumpstream];
					*stam_base = max<float>(a, b);
				} break;
				case Skill_Chordjack:
					*adj_diff *= fastsqrt(calc.doot.at(hi).at(CJOHJump).at(i));
					break;
				case Skill_Technical:
					*adj_diff =
					  calc.soap.at(hi).at(TechBase).at(i) * tp_mods.at(ss) *
					  basescalers.at(ss) /
					  max<float>(fastpow(calc.doot.at(hi).at(CJ).at(i), 2.F),
								 1.F) /
					  fastsqrt(calc.doot.at(hi).at(OHJumpMod).at(i));
					break;
				default:
					break;
			}
		}
	}
}

inline void
make_debug_strings(const Calc& calc, vector<std::string>& debugstrings)
{
	debugstrings.resize(calc.numitv);

	for (int itv = 0; itv < calc.numitv; ++itv) {
		std::string itvstring = "";

		for (int row = 0; row < calc.itv_size.at(itv); ++row) {
			const auto& ri = calc.adj_ni.at(itv).at(row);

			itvstring.append(note_mapping[ri.row_notes].second);
			itvstring.append("\n");
		}

		if (!itvstring.empty())
			itvstring.pop_back();
		debugstrings.at(itv) = itvstring;
	}
}

static const float ssr_goal_cap = 0.965F; // goal cap to prevent insane scaling
// Function to generate SSR rating
auto
MinaSDCalc(const vector<NoteInfo>& NoteInfo,
		   float musicrate,
		   float goal,
		   Calc* calc) -> vector<float>
{
	if (NoteInfo.size() <= 1) {
		return dimples_the_all_zero_output;
	}
	calc->ssr = true;
	calc->debugmode = false;

	return calc->CalcMain(NoteInfo, musicrate, min(goal, ssr_goal_cap));
}

// Wrap difficulty calculation for all standard rates
auto
MinaSDCalc(const vector<NoteInfo>& NoteInfo, Calc* calc) -> MinaSD
{
	MinaSD allrates;
	int lower_rate = 7;
	int upper_rate = 21;

	if (NoteInfo.size() > 1) {
		calc->ssr = false;
		calc->debugmode = false;
		for (int i = lower_rate; i < upper_rate; i++) {
			allrates.emplace_back(
			  calc->CalcMain(NoteInfo, static_cast<float>(i) / 10.F, 0.93F));
		}
	} else {
		for (int i = lower_rate; i < upper_rate; i++) {
			allrates.emplace_back(dimples_the_all_zero_output);
		}
	}
	return allrates;
}

// Debug output
void
MinaSDCalcDebug(const vector<NoteInfo>& NoteInfo,
				float musicrate,
				float goal,
				vector<vector<vector<vector<float>>>>& handInfo,
				vector<std::string>& debugstrings,
				Calc& calc)
{
	if (NoteInfo.size() <= 1) {
		return;
	}

	calc.debugmode = true;
	calc.ssr = true;
	calc.CalcMain(NoteInfo, musicrate, min(goal, ssr_goal_cap));
	make_debug_strings(calc, debugstrings);

	handInfo.emplace_back(calc.debugValues.at(left_hand));
	handInfo.emplace_back(calc.debugValues.at(right_hand));

	// asdkfhjasdkfhaskdfjhasfd
	if (!DoesFileExist(calc_params_xml)) {
		TheGreatBazoinkazoinkInTheSky ublov(calc);
		ublov.write_params_to_disk();
	}
}

int mina_calc_version = 408;
auto
GetCalcVersion() -> int
{
	return mina_calc_version;
}
