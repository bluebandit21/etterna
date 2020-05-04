#include "MinaCalc.h"
#include <cmath>
#include <iostream>
#include <algorithm>
#include <array>
#include <memory>
#include <numeric>
#include <xmmintrin.h>
#include <cstring>
#include <string>
#include <set>

using std::max;
using std::min;
using std::pow;
using std::set;
using std::sqrt;
using std::vector;

// Relies on endiannes (significantly inaccurate)
inline double
fastpow(double a, double b)
{
	int u[2];
	std::memcpy(&u, &a, sizeof a);
	u[1] = static_cast<int>(b * (u[1] - 1072632447) + 1072632447);
	u[0] = 0;
	std::memcpy(&a, &u, sizeof a);
	return a;
}

// reasonably accurate taylor approximation for ^ 1.7 (jk not anymore not
// really)
inline float
fast_pw(float x)
{
	float xbar = x - 0.5f;
	return 0.287175f + 1.13383f * xbar + 0.527063f * xbar * xbar;
}

// not super accurate, good enough for our purposes
inline float
fastsqrt(float _in)
{
	if (_in == 0.f)
		return 0.f;
	__m128 in = _mm_load_ss(&_in);
	float out;
	_mm_store_ss(&out, _mm_mul_ss(in, _mm_rsqrt_ss(in)));
	return out;
}

template<typename T>
inline T
CalcClamp(T x, T l, T h)
{
	return x > h ? h : (x < l ? l : x);
}

inline float
mean(const vector<float>& v)
{
	return std::accumulate(begin(v), end(v), 0.f) / v.size();
}

// Coefficient of variation
inline float
cv(const vector<float>& input)
{
	float sd = 0.f;
	float average = mean(input);
	for (float i : input)
		sd += (i - average) * (i - average);

	return fastsqrt(sd / input.size()) / average;
}

inline float
downscale_low_accuracy_scores(float f, float sg)
{
	return sg >= 0.93f ? f : min(max(f / sqrt(1.f + (0.93f - sg)), 0.f), 100.f);
}

inline void
Smooth(vector<float>& input, float neutral)
{
	float f1;
	float f2 = neutral;
	float f3 = neutral;

	for (float& i : input) {
		f1 = f2;
		f2 = f3;
		f3 = i;
		i = (f1 + f2 + f3) / 3;
	}
}

inline void
DifficultyMSSmooth(vector<float>& input)
{
	float f1;
	float f2 = 0.f;

	for (float& i : input) {
		f1 = f2;
		f2 = i;
		i = (f1 + f2) / 2.f;
	}
}

inline float
AggregateScores(const vector<float>& skillsets, float rating, float resolution)
{
	float sum;
	for (int iter = 1; iter <= 11; iter++) {
		do {
			rating += resolution;
			sum = 0.0f;
			for (float i : skillsets) {
				sum += 2.f / std::erfc(0.5f * (i - rating)) - 1.f;
			}
		} while (3 < sum);
		rating -= resolution;
		resolution /= 2.f;
	}
	return rating + 2.f * resolution;
}

inline unsigned int
column_count(unsigned int note)
{
	return note % 2 + note / 2 % 2 + note / 4 % 2 + note / 8 % 2;
}

float
chord_proportion(const vector<NoteInfo>& NoteInfo, const int chord_size)
{
	unsigned int taps = 0;
	unsigned int chords = 0;

	for (auto row : NoteInfo) {
		unsigned int notes = column_count(row.notes);
		taps += notes;
		if (notes == chord_size)
			chords += notes;
	}

	return static_cast<float>(chords) / static_cast<float>(taps);
}

vector<float>
skillset_vector(const DifficultyRating& difficulty)
{
	return vector<float>{ difficulty.overall,	difficulty.stream,
						  difficulty.jumpstream, difficulty.handstream,
						  difficulty.stamina,	difficulty.jack,
						  difficulty.chordjack,  difficulty.technical };
}

inline float
highest_difficulty(const DifficultyRating& difficulty)
{
	auto v = { difficulty.stream,	 difficulty.jumpstream,
			   difficulty.handstream, difficulty.stamina,
			   difficulty.jack,		  difficulty.chordjack,
			   difficulty.technical };
	return *std::max_element(v.begin(), v.end());
}

void
Calc::TotalMaxPoints()
{
	MaxPoints = 0;
	for (size_t i = 0; i < left_hand.v_itvpoints.size(); i++)
		MaxPoints += left_hand.v_itvpoints[i] + right_hand.v_itvpoints[i];
}

void
Hand::InitPoints(const Finger& f1, const Finger& f2)
{
	v_itvpoints.clear();
	for (size_t ki_is_rising = 0; ki_is_rising < f1.size(); ++ki_is_rising)
		v_itvpoints.emplace_back(f1[ki_is_rising].size() +
								 f2[ki_is_rising].size());
}

float
Calc::JackLoss(const vector<float>& j, float x)
{
	float o = 0.f;
	for (size_t i = 0; i < j.size(); i++)
		if (x < j[i])
			o += 7.f - (7.f * pow(x / (j[i] * 0.88f), 1.7f));
	CalcClamp(o, 0.f, 10000.f);
	return o;
}

JackSeq
Calc::SequenceJack(const vector<NoteInfo>& NoteInfo,
				   unsigned int t,
				   float music_rate)
{
	vector<float> output;
	float last = -5.f;
	float interval1 = 250.f;
	float interval2 = 250.f;
	float interval3 = 250.f;
	float interval4 = 250.f;
	unsigned int track = 1u << t;

	for (auto i : NoteInfo) {
		if (i.notes & track) {
			float current_time = i.rowTime / music_rate;
			interval1 = interval2;
			interval2 = interval3;
			interval3 = interval4;
			interval4 = 1000.f * (current_time - last);
			last = current_time;
			output.emplace_back(min(
			  2750.f /
				min(
				  (interval2 + interval3 + interval4) / 3.f,
				  0.8f * interval4 *
					CalcClamp(
					  1.f + cv({ interval1, interval2, interval3, interval4 }),
					  1.f,
					  1.8f)),
			  45.f));
		}
	}
	return output;
}

Finger
Calc::ProcessFinger(const vector<NoteInfo>& NoteInfo,
					unsigned int t,
					float music_rate,
					float offset)
{
	// optimization, just allocate memory here once and recycle this vector
	vector<float> temp_queue(5000);
	vector<int> temp_queue_two(5000);
	unsigned int row_counter = 0;
	unsigned int row_counter_two = 0;

	int Interval = 0;
	float last = -5.f;
	Finger AllIntervals(numitv, vector<float>());
	if (t == 0)
		nervIntervals = vector<vector<int>>(numitv, vector<int>());
	unsigned int column = 1u << t;

	for (size_t i = 0; i < NoteInfo.size(); i++) {
		float scaledtime = (NoteInfo[i].rowTime / music_rate) + offset;

		while (scaledtime > static_cast<float>(Interval + 1) * IntervalSpan) {
			// dump stored values before iterating to new interval
			// we're in a while loop to skip through empty intervals
			// so check the counter to make sure we didn't already assign
			if (row_counter > 0) {
				AllIntervals[Interval].resize(row_counter);
				for (unsigned int n = 0; n < row_counter; ++n)
					AllIntervals[Interval][n] = temp_queue[n];
			}

			if (row_counter_two > 0) {
				nervIntervals[Interval].resize(row_counter_two);
				for (unsigned int n = 0; n < row_counter_two; ++n)
					nervIntervals[Interval][n] = temp_queue_two[n];
			}

			// reset the counter and iterate interval
			row_counter = 0;
			row_counter_two = 0;
			++Interval;
		}

		if (NoteInfo[i].notes & column) {
			// log all rows for this interval in pre-allocated mem
			temp_queue[row_counter] =
			  CalcClamp(1000.f * (scaledtime - last), 40.f, 5000.f);
			++row_counter;
			last = scaledtime;
		}

		if (t == 0 && NoteInfo[i].notes != 0) {
			temp_queue_two[row_counter_two] = i;
			++row_counter_two;
		}
	}
	return AllIntervals;
}

vector<float>
Calc::CalcMain(const vector<NoteInfo>& NoteInfo,
			   float music_rate,
			   float score_goal)
{
	float grindscaler =
	  CalcClamp(
		0.93f + (0.07f * (NoteInfo.back().rowTime - 30.f) / 30.f), 0.93f, 1.f) *
	  CalcClamp(
		0.873f + (0.13f * (NoteInfo.back().rowTime - 15.f) / 15.f), 0.87f, 1.f);

	float shortstamdownscaler = CalcClamp(
	  0.9f + (0.1f * (NoteInfo.back().rowTime - 150.f) / 150.f), 0.9f, 1.f);

	static const int fo_rizzy = 1;
	vector<vector<float>> the_hizzle_dizzles(fo_rizzy);
	for (int WHAT_IS_EVEN_HAPPEN_THE_BOMB = 0;
		 WHAT_IS_EVEN_HAPPEN_THE_BOMB < fo_rizzy;
		 ++WHAT_IS_EVEN_HAPPEN_THE_BOMB) {
		InitializeHands(
		  NoteInfo, music_rate, 0.2f * WHAT_IS_EVEN_HAPPEN_THE_BOMB);
		TotalMaxPoints();

		vector<float> mcbloop(NUM_Skillset);
		// overall and stam will be left as 0.f by this loop
		for (int i = 0; i < NUM_Skillset; ++i)
			mcbloop[i] = Chisel(0.1f, 10.24f, score_goal, i, false);

		// stam is based on which calc produced the highest output without it
		size_t highest_base_skillset = std::distance(
		  mcbloop.begin(), std::max_element(mcbloop.begin(), mcbloop.end()));
		float base = mcbloop[highest_base_skillset];

		// rerun all with stam on, optimize by starting at the non-stam adjusted
		// base value for each skillset
		// we can actually set the stam floor to < 1 to shift the curve a bit
		for (int i = 0; i < NUM_Skillset; ++i)
			mcbloop[i] = Chisel(mcbloop[i] * 0.95f, 0.64f, score_goal, i, true);

		// all relative scaling to specific skillsets should occur before this
		// point, not after (it ended up this way due to the normalizers which
		// were dumb and removed) stam is the only skillset that can/should be
		// normalized to base values without interfering with anything else
		// (since it's not based on a type of pattern)

		// stam jams, stamina should push up the base ratings for files so files
		// that are more difficult by virtue of being twice as long for more or
		// less the same patterns don't get underrated, however they shouldn't
		// be pushed up a huge amount either, we want high stream scores to be
		// equally achieveable on longer or shorter files, ideally, the stam
		// ratings itself is a separate consideration and will be scaled to the
		// degree to which the stamina model affects the base rating, so while
		// stamina should affect the base skillset ratings slightly we want the
		// degree to which it makes files harder to be catalogued as the stamina
		// rating scaling down stuff that has no stamina component will help
		// preventing pollution of stamina leaderboards with charts that are
		// just very high rated but take no stamina
		float poodle_in_a_porta_potty = mcbloop[highest_base_skillset];

		// ends up being a multiplier between ~0.8 and ~1
		// tuning is a wip
		float mcfroggerbopper =
		  pow((poodle_in_a_porta_potty / base) - 0.05f, 2.5f);

		// we wanted to shift the curve down a lot before pow'ing but it was too
		// much to balance out, so we need to give some back, this is roughly
		// equivalent of multiplying by 1.05 but also not really because math
		// we don't want to push up the high end stuff anymore so just add to
		// let stuff down the curve catch up a little
		// remember we're operating on a multiplier
		mcfroggerbopper = CalcClamp(mcfroggerbopper + 0.025f, 0.8f, 1.09f);
		mcbloop[Skill_Stamina] = poodle_in_a_porta_potty * mcfroggerbopper;

		// yes i know how dumb this looks
		DifficultyRating difficulty = { mcbloop[0], mcbloop[1], mcbloop[2],
										mcbloop[3], mcbloop[4], mcbloop[5],
										mcbloop[6], mcbloop[7] };
		vector<float> pumpkin = skillset_vector(difficulty);
		// sets the 'proper' debug output, doesn't (shouldn't) affect actual
		// values this is the only time debugoutput arg should be set to true
		if (debugmode)
			Chisel(mcbloop[highest_base_skillset] - 0.16f,
				   0.32f,
				   score_goal,
				   highest_base_skillset,
				   true,
				   true);

		difficulty.overall = highest_difficulty(difficulty);

		// the final push down, cap ssrs (score specific ratings) to stop vibro
		// garbage and calc abuse from polluting leaderboards too much, a "true"
		// 38 is still unachieved so a cap of 40 [sic] is _extremely_ generous
		// do this for SCORES only, not cached file difficulties
		auto bye_vibro_maybe_yes_this_should_be_refactored_lul =
		  skillset_vector(difficulty);
		if (capssr) {
			static const float ssrcap = 40.f;

			for (auto& r : bye_vibro_maybe_yes_this_should_be_refactored_lul) {
				// so 50%s on 60s don't give 35s
				r = downscale_low_accuracy_scores(r, score_goal);
				r = CalcClamp(r, r, ssrcap);
			}
		}
		for (size_t bagles = 0;
			 bagles < bye_vibro_maybe_yes_this_should_be_refactored_lul.size();
			 ++bagles)
			the_hizzle_dizzles[WHAT_IS_EVEN_HAPPEN_THE_BOMB].push_back(bye_vibro_maybe_yes_this_should_be_refactored_lul[bagles]);
	}
	vector<float> yo_momma(NUM_Skillset);
	for (size_t farts = 0; farts < the_hizzle_dizzles[0].size(); ++farts) {
		vector<float> girls;
		for (size_t nibble = 0; nibble < the_hizzle_dizzles.size(); ++nibble) {
			girls.push_back(the_hizzle_dizzles[nibble][farts]);
			
		}
		yo_momma[farts] = mean(girls);
		girls.clear();
	}

	return yo_momma;
}

void
Calc::InitializeHands(const vector<NoteInfo>& NoteInfo,
					  float music_rate,
					  float offset)
{
	numitv = static_cast<int>(
	  std::ceil(NoteInfo.back().rowTime / (music_rate * IntervalSpan)));

	// these get changed/updated frequently so allocate them once at the start
	left_hand.adj_diff.resize(numitv);
	right_hand.adj_diff.resize(numitv);
	left_hand.stam_adj_diff.resize(numitv);
	right_hand.stam_adj_diff.resize(numitv);

	// at least for the moment there are a few mods we want to apply evenly
	// to all skillset, so pre-multiply them in these after they're generated
	left_hand.pre_multiplied_pattern_mod_group_a.resize(numitv);
	right_hand.pre_multiplied_pattern_mod_group_a.resize(numitv);

	ProcessedFingers fingers;
	for (int i = 0; i < 4; i++)
		fingers.emplace_back(ProcessFinger(NoteInfo, i, music_rate, offset));

	// initialize base difficulty and point values
	left_hand.InitDiff(fingers[0], fingers[1]);
	left_hand.InitPoints(fingers[0], fingers[1]);
	right_hand.InitDiff(fingers[2], fingers[3]);
	right_hand.InitPoints(fingers[2], fingers[3]);

	// set pattern mods
	SetAnchorMod(NoteInfo, 1, 2, left_hand.doot);
	SetAnchorMod(NoteInfo, 4, 8, right_hand.doot);

	SetSequentialDownscalers(NoteInfo, 1, 2, music_rate, left_hand.doot);
	SetSequentialDownscalers(NoteInfo, 4, 8, music_rate, right_hand.doot);

	// these are evaluated on all columns so right and left are the same
	// these also may be redundant with updated stuff
	SetHSMod(NoteInfo, left_hand.doot);
	SetJumpMod(NoteInfo, left_hand.doot);
	SetCJMod(NoteInfo, left_hand.doot);
	SetStreamMod(NoteInfo, left_hand.doot, music_rate);
	SetFlamJamMod(NoteInfo, left_hand.doot, music_rate);
	right_hand.doot[HS] = left_hand.doot[HS];
	right_hand.doot[Jump] = left_hand.doot[Jump];
	right_hand.doot[CJ] = left_hand.doot[CJ];
	right_hand.doot[StreamMod] = left_hand.doot[StreamMod];
	right_hand.doot[Chaos] = left_hand.doot[Chaos];
	right_hand.doot[FlamJam] = left_hand.doot[FlamJam];

	// roll and ohj, set these after chaos mod has been calculated so we can
	// nerf the poly mod based on the roll mod, we don't want psuedo rolls
	// formed by polys to get the poly bonus if at all possible
	SetSequentialDownscalers(NoteInfo, 1, 2, music_rate, left_hand.doot);
	SetSequentialDownscalers(NoteInfo, 4, 8, music_rate, right_hand.doot);

	// pattern mods and base msd never change so set them immediately
	if (debugmode) {
		left_hand.debugValues.resize(3);
		right_hand.debugValues.resize(3);
		left_hand.debugValues[0].resize(ModCount);
		right_hand.debugValues[0].resize(ModCount);
		left_hand.debugValues[1].resize(NUM_CalcDiffValue);
		right_hand.debugValues[1].resize(NUM_CalcDiffValue);
		left_hand.debugValues[2].resize(NUM_CalcDebugMisc);
		right_hand.debugValues[2].resize(NUM_CalcDebugMisc);

		for (size_t i = 0; i < ModCount; ++i) {
			left_hand.debugValues[0][i] = left_hand.doot[i];
			right_hand.debugValues[0][i] = right_hand.doot[i];
		}

		// set everything but final adjusted output here
		for (size_t i = 0; i < NUM_CalcDiffValue - 1; ++i) {
			left_hand.debugValues[1][i] = left_hand.soap[i];
			right_hand.debugValues[1][i] = right_hand.soap[i];
		}
	}

	// it's probably time to loop over hands more sensibly or
	// do this stuff inside the class
	for (int i = 0; i < numitv; ++i) {
		left_hand.pre_multiplied_pattern_mod_group_a[i] =
		  left_hand.doot[Roll][i] * left_hand.doot[OHJump][i] *
		  left_hand.doot[Anchor][i];
		right_hand.pre_multiplied_pattern_mod_group_a[i] =
		  right_hand.doot[Roll][i] * right_hand.doot[OHJump][i] *
		  right_hand.doot[Anchor][i];
	}

	j0 = SequenceJack(NoteInfo, 0, music_rate);
	j1 = SequenceJack(NoteInfo, 1, music_rate);
	j2 = SequenceJack(NoteInfo, 2, music_rate);
	j3 = SequenceJack(NoteInfo, 3, music_rate);
}

// DON'T WANT TO RECOMPILE HALF THE GAME IF I EDIT THE HEADER FILE
static const float finalscaler = 2.564f * 1.05f * 1.1f * 1.10f * 1.10f *
								 1.025f; // multiplier to standardize baselines

// ***note*** if we want max control over stamina we need to have one model for
// affecting the other skillsets to a certain degree, enough to push up longer
// stream ratings into contention with shorter ones, and another for both a more
// granular and influential modifier to calculate the end stamina rating with
// so todo on that

// Stamina Model params
static const float stam_ceil = 1.071234f; // stamina multiplier max
static const float stam_mag = 323.f;	  // multiplier generation scaler
static const float stam_fscale = 400.f; // how fast the floor rises (it's lava)
static const float stam_prop =
  0.7444f; // proportion of player difficulty at which stamina tax begins

// since we are no longer using the normalizer system we need to lower
// the base difficulty for each skillset and then detect pattern types
// to push down OR up, rather than just down and normalizing to a differential
// since chorded patterns have lower enps than streams, streams default to 1
// and chordstreams start lower
// stam is a special case and may use normalizers again
static const float basescalers[NUM_Skillset] = { 0.f, 0.975f, 0.94f, 0.95f,
												 0.f, 0.8f,   0.85f, 0.9f };

float
Hand::CalcMSEstimate(vector<float>& input)
{
	if (input.empty())
		return 0.f;

	sort(input.begin(), input.end());
	float m = 0;
	input[0] *= 1.066f; // This is gross
	size_t End = min(input.size(), static_cast<size_t>(6));
	for (size_t i = 0; i < End; i++)
		m += input[i];
	return 1375.f * End / m;
}

void
Hand::InitDiff(Finger& f1, Finger& f2)
{
	for (size_t i = 0; i < NUM_CalcDiffValue - 1; ++i)
		soap[i].resize(f1.size());

	for (size_t i = 0; i < f1.size(); i++) {
		float nps = 1.6f * static_cast<float>(f1[i].size() + f2[i].size());
		float left_difficulty = CalcMSEstimate(f1[i]);
		float right_difficulty = CalcMSEstimate(f2[i]);
		float difficulty = max(left_difficulty, right_difficulty);
		soap[BaseNPS][i] = finalscaler * nps;
		soap[BaseMS][i] = finalscaler * difficulty;
		soap[BaseMSD][i] = finalscaler * (6.f * difficulty + 3.f * nps) / 9.f;
	}
	Smooth(soap[BaseNPS], 0.f);
	if (SmoothDifficulty)
		DifficultyMSSmooth(soap[BaseMSD]);
}

// each skillset should just be a separate calc function [todo]
float
Calc::Chisel(float player_skill,
			 float resolution,
			 float score_goal,
			 int ss,
			 bool stamina,
			 bool debugoutput)
{
	float gotpoints = 0.f;
	int possiblepoints = 0;
	float reqpoints = static_cast<float>(MaxPoints) * score_goal;
	for (int iter = 1; iter <= 8; iter++) {
		do {
			if (player_skill > 100.f)
				return player_skill;
			player_skill += resolution;
			if (ss == Skill_Overall || ss == Skill_Stamina)
				return 0.f; // not how we set these values

			// reset tallied score
			gotpoints = 0.f;
			possiblepoints = 0;

			// jack sequencer point loss for jack speed and (maybe?) cj
			if (ss == Skill_JackSpeed)
				gotpoints =
				  MaxPoints +
				  (JackLoss(j0, player_skill) - JackLoss(j1, player_skill) -
				   JackLoss(j2, player_skill) - JackLoss(j3, player_skill));
			else {
				if (ss == Skill_Chordjack)
					gotpoints -= sqrt(abs(
					  JackLoss(j0, player_skill) - JackLoss(j1, player_skill) -
					  JackLoss(j2, player_skill) - JackLoss(j3, player_skill)));
				// if (debugoutput)
				// std::cout << "jackloss: " <<
				// (JackLoss(j0, player_skill) - JackLoss(j1, player_skill) -
				// JackLoss(j2, player_skill) - JackLoss(j3, player_skill)) <<
				// std::endl;
				// we _don't_ want pure jack files to be listed as technical but
				// we also don't want to depress technical files with moderate
				// jacks
				// if (ss == Skill_Technical)
				//	gotpoints -=
				//	  max(reqpoints * -0.5f,  (JackLoss(j0, player_skill) -
				// JackLoss(j1, player_skill) - 	   JackLoss(j2,
				// player_skill)
				// - JackLoss(j3, player_skill)) / 	  4.f);

				// run standard calculator stuffies
				left_hand.CalcInternal(gotpoints, player_skill, ss, stamina);
				right_hand.CalcInternal(gotpoints, player_skill, ss, stamina);
			}

		} while (gotpoints < reqpoints);
		player_skill -= resolution;
		resolution /= 2.f;
	}

	// these are the values for msd/stam adjusted msd/pointloss the
	// latter two are dependent on player_skill and so should only
	// be recalculated with the final value already determined
	if (debugoutput) {
		left_hand.CalcInternal(
		  gotpoints, player_skill, ss, stamina, debugoutput);
		right_hand.CalcInternal(
		  gotpoints, player_skill, ss, stamina, debugoutput);
	}

	return player_skill + 2.f * resolution;
}

// debug bool here is NOT the one in Calc, it is passed from chisel using the
// final difficulty as the starting point and should only be executed once per
// chisel
void
Hand::CalcInternal(float& gotpoints, float& x, int ss, bool stam, bool debug)
{
	// we're going to recycle adj_diff for this part
	for (size_t i = 0; i < soap[BaseNPS].size(); ++i) {
		// the new way we wil attempt to diffrentiate skillsets rather than
		// using normalizers is by detecting whether or not we think a file is
		// mostly comprised of a given pattern, producing a downscaler that
		// slightly buffs up those files and produces a downscaler for files not
		// detected of that type. the major potential failing of this system is
		// that it ends up such that the rating is tied directly to whether or
		// not a file can be more or less strongly determined to be of a pattern
		// type, e.g. splithand trills being marked as more "js" than actual js,
		// for the moment these modifiers are still built on proportion of taps
		// in chords / total taps, but there's a lot more give than their used
		// to be. they should be re-done as sequential detection for best effect
		// but i don't know if that will be necessary for basic tuning
		// if we don't do this files may end up misclassing hard and polluting
		// leaderboards, and good scores on overrated files will simply produce
		// high ratings in every category
		switch (ss) {
				// streammod downscales anything not single tap focused
			case Skill_Stream:
				adj_diff[i] = soap[BaseNPS][i] * doot[FlamJam][i] *
							  doot[StreamMod][i] * doot[Chaos][i];
				break;
				// jump downscales anything without some jumps
			case Skill_Jumpstream:
				adj_diff[i] = soap[BaseNPS][i] * doot[Jump][i] * doot[Chaos][i];
				break;
				// hs downscales anything without some hands
			case Skill_Handstream:
				adj_diff[i] = soap[BaseNPS][i] * doot[HS][i];
				break;
			case Skill_JackSpeed: // don't use ms hybrid base
				adj_diff[i] =
				  soap[BaseMSD][i] *
				  max(max(doot[StreamMod][i], doot[Jump][i]), doot[HS][i]);
				break;
			case Skill_Chordjack: // don't use ms hybrid base
				adj_diff[i] = soap[BaseNPS][i] * doot[CJ][i];
				break;
			case Skill_Technical: // use ms hybrid base
				adj_diff[i] =
				  soap[BaseMSD][i] * doot[Chaos][i] *
				  max(max(doot[StreamMod][i], doot[Jump][i]), doot[HS][i]);
				break;
			case Skill_Stamina: // handled up the stack, never happens here
			case Skill_Overall: // handled up the stack, never happens here
				break;
		}

		// we always want to apply these mods, i think (roll, anchor, ohjump)
		adj_diff[i] *= pre_multiplied_pattern_mod_group_a[i] * basescalers[ss];
	}

	if (stam)
		StamAdjust(x, adj_diff);

	// final difficulty values to use
	const vector<float>& v = stam ? stam_adj_diff : adj_diff;

	// i don't like the copypasta either but the boolchecks where they were
	// were too slow
	if (debug) {
		debugValues[2][StamMod].resize(v.size());
		debugValues[2][PtLoss].resize(v.size());
		// final debug output should always be with stam activated
		StamAdjust(x, adj_diff, true);
		debugValues[1][MSD] = stam_adj_diff;

		for (size_t i = 0; i < v.size(); ++i) {
			float gainedpoints = x > v[i] ? static_cast<float>(v_itvpoints[i])
										  : static_cast<float>(v_itvpoints[i]) *
											  fastpow(x / v[i], 1.7f);
			gotpoints += gainedpoints;
			debugValues[2][PtLoss][i] =
			  (static_cast<float>(v_itvpoints[i]) - gainedpoints);
		}
	} else
		for (size_t i = 0; i < v.size(); ++i)
			gotpoints += x > v[i] ? static_cast<float>(v_itvpoints[i])
								  : static_cast<float>(v_itvpoints[i]) *
									  fastpow(x / v[i], 1.7f);
}

void
Hand::StamAdjust(float x, vector<float>& diff, bool debug)
{
	float floor = 0.95f; // stamina multiplier min (increases as chart advances)
	float mod = 0.95f;   // mutliplier

	float avs1 = 0.f;
	float avs2 = 0.f;

	// i don't like the copypasta either but the boolchecks where they were
	// were too slow
	if (debug)
		for (size_t i = 0; i < diff.size(); i++) {
			avs1 = avs2;
			avs2 = diff[i];
			mod += ((((avs1 + avs2) / 2.f) / (stam_prop * x)) - 1.f) / stam_mag;
			if (mod > 1.f)
				if (floor < stam_ceil)
					floor += (mod - 1.f) / stam_fscale;

			mod = CalcClamp(mod, floor, stam_ceil);
			stam_adj_diff[i] = avs2 * mod;
			debugValues[2][StamMod][i] = mod;
		}
	else
		for (size_t i = 0; i < diff.size(); i++) {
			avs1 = avs2;
			avs2 = diff[i];
			mod += ((((avs1 + avs2) / 2.f) / (stam_prop * x)) - 1.f) / stam_mag;
			if (mod > 1.f)
				if (floor < stam_ceil)
					floor += (mod - 1.f) / stam_fscale;

			mod = CalcClamp(mod, floor, stam_ceil);
			stam_adj_diff[i] = avs2 * mod;
		}
}

void
Calc::SetAnchorMod(const vector<NoteInfo>& NoteInfo,
				   unsigned int firstNote,
				   unsigned int secondNote,
				   vector<float> doot[ModCount])
{
	doot[Anchor].resize(nervIntervals.size());

	for (size_t i = 0; i < nervIntervals.size(); i++) {
		int lcol = 0;
		int rcol = 0;
		for (int row : nervIntervals[i]) {
			if (NoteInfo[row].notes & firstNote)
				++lcol;
			if (NoteInfo[row].notes & secondNote)
				++rcol;
		}
		bool anyzero = lcol == 0 || rcol == 0;
		float bort = static_cast<float>(min(lcol, rcol)) /
					 static_cast<float>(max(lcol, rcol));
		bort = (0.3f + (1.f + (1.f / bort)) / 4.f);

		//
		bort = CalcClamp(bort, 0.9f, 1.1f);

		doot[Anchor][i] = anyzero ? 1.f : bort;

		fingerbias += (static_cast<float>(max(lcol, rcol)) + 2.f) /
					  (static_cast<float>(min(lcol, rcol)) + 1.f);
	}

	if (SmoothPatterns)
		Smooth(doot[Anchor], 1.f);
}

void
Calc::SetHSMod(const vector<NoteInfo>& NoteInfo, vector<float> doot[ModCount])
{
	doot[HS].resize(nervIntervals.size());

	for (size_t i = 0; i < nervIntervals.size(); i++) {
		// sequencing stuff
		int actual_jacks = 0;
		int not_stream = 0;
		int last_cols = 0;
		int col_id[4] = { 1, 2, 4, 8 };

		unsigned int taps = 0;
		unsigned int handtaps = 0;
		unsigned int last_notes = 0;
		for (int row : nervIntervals[i]) {
			unsigned int notes = column_count(NoteInfo[row].notes);
			taps += notes;
			if (notes == 3)
				handtaps += 3;

			// sequencing stuff
			unsigned int cols = NoteInfo[row].notes;
			for (auto& id : col_id)
				if (cols & id && last_cols & id)
					++actual_jacks;

			// suppress jumptrilly garbage a little bit
			if (last_notes == 1 && notes > 1)
				++not_stream;
			else if (last_notes > 1 && notes == 1)
				++not_stream;
			last_notes = notes;
			last_cols = cols;
		}

		if (taps == 0) // nothing here
			doot[HS][i] = 1.f;
		else if (taps < 3) // look ma no hands
			doot[HS][i] = 0.8f;
		else { // at least 1 hand
			// when bark of dog into canyon scream at you
			float prop = static_cast<float>(handtaps + 1) /
						 static_cast<float>(taps - 1) * 32.f / 7.f;

			float bromide = CalcClamp(4.f - not_stream, 0.975f, 1.f);
			// downscale by jack density rather than upscale, like cj
			float brop = CalcClamp(3.f - actual_jacks, 0.8f, 1.f);
			// clamp the original prop mod first before applying above
			float zoot = CalcClamp(sqrt(prop), 0.8f, 1.025f);
			doot[HS][i] = CalcClamp(zoot * bromide * brop, 0.8f, 1.025f);
		}
	}

	if (SmoothPatterns)
		Smooth(doot[HS], 1.f);
}

void
Calc::SetJumpMod(const vector<NoteInfo>& NoteInfo, vector<float> doot[ModCount])
{
	doot[Jump].resize(nervIntervals.size());

	for (size_t i = 0; i < nervIntervals.size(); i++) {
		// sequencing stuff
		int actual_jacks = 0;
		int not_stream = 0;
		int last_cols = 0;
		int col_id[4] = { 1, 2, 4, 8 };

		unsigned int taps = 0;
		unsigned int jumptaps = 0;
		unsigned int last_notes = 0;
		for (int row : nervIntervals[i]) {
			unsigned int notes = column_count(NoteInfo[row].notes);
			taps += notes;
			if (notes == 2)
				jumptaps += 2;

			// sequencing stuff
			unsigned int cols = NoteInfo[row].notes;
			for (auto& id : col_id)
				if (cols & id && last_cols & id)
					++actual_jacks;

			// suppress jumptrilly garbage a little bit, this is redundant in
			// some cases with ohjump downscaler so we can't go too ham
			if (last_notes == 1)
				if (notes == 1)
					++not_stream;
			if (last_notes > 1)
				if (notes > 1)
					++not_stream;
			last_notes = notes;
			last_cols = cols;
		}

		if (taps == 0) // nothing here
			doot[Jump][i] = 1.f;
		else if (taps < 2) // at least 1 tap but no jumps
			doot[Jump][i] = 0.8f;
		else { // at least 1 jump
			// creepy banana
			float prop = static_cast<float>(jumptaps + 1) /
						 static_cast<float>(taps - 1) * 20.f / 7.f;

			float bromide = CalcClamp(5.f - not_stream, 0.975f, 1.f);
			// downscale by jack density rather than upscale, like cj
			float brop = CalcClamp(3.f - actual_jacks, 0.95f, 1.f);
			// clamp the original prop mod first before applying above
			float zoot = CalcClamp(sqrt(prop), 0.8f, 1.025f);
			doot[Jump][i] = CalcClamp(zoot * bromide * brop, 0.8f, 1.025f);
		}
	}
	if (SmoothPatterns)
		Smooth(doot[Jump], 1.f);
}

// depress cj rating for non-cj stuff and boost cj rating for cj stuff
void
Calc::SetCJMod(const vector<NoteInfo>& NoteInfo, vector<float> doot[])
{
	doot[CJ].resize(nervIntervals.size());
	for (size_t i = 0; i < nervIntervals.size(); i++) {
		// sequencing stuff
		int actual_jacks = 0;
		int last_cols = 0;
		int col_id[4] = { 1, 2, 4, 8 };

		unsigned int taps = 0;
		unsigned int chordtaps = 0;
		for (int row : nervIntervals[i]) {
			unsigned int notes = column_count(NoteInfo[row].notes);
			taps += notes;
			if (notes > 1)
				chordtaps += notes;

			// sequencing stuff
			unsigned int cols = NoteInfo[row].notes;
			for (auto& id : col_id)
				if (cols & id && last_cols & id) {
					++actual_jacks;
					// if we don't break we're saying something like "chordjacks
					// are harder if they share more columns from chord to
					// chord" which is not true, it is in fact either irrelevant
					// or the inverse depending on the scenario, this is merely
					// to catch stuff like splithand jumptrills registering as
					// chordjacks when they shouldn't be
					break;
				}

			last_cols = cols;
		}

		if (taps == 0) // nothing here
			doot[CJ][i] = 1.f;
		else if (chordtaps == 0) { // there are taps, but no chords
			doot[CJ][i] = 0.7f;
		} else { // we have at least 1 chord
			// we want to give a little leeway for single taps but not too much
			// or sections of [12]4[123]   [123]4[23] will be flagged as
			// chordjack when they're really just broken chordstream, and we
			// also want to give enough leeway so that hyperdense chordjacks at
			// lower bpms aren't automatically rated higher than more sparse
			// jacks at higher bpms
			float prop = (chordtaps + 1) / (taps - 1) * 13.f / 7.f;
			float brop = CalcClamp(actual_jacks - 2.f, 0.5f, 1.f);
			doot[CJ][i] = CalcClamp(brop * sqrt(prop), 0.7f, 1.1f);
		}
	}
	if (SmoothPatterns)
		Smooth(doot[CJ], 1.f);
}

// try to sniff out chords that are built as flams. BADLY NEEDS REFACTOR
void
Calc::SetFlamJamMod(const vector<NoteInfo>& NoteInfo,
					vector<float> doot[],
					float& music_rate)
{
	doot[FlamJam].resize(nervIntervals.size());
	// scan for flam chords in this window
	float grouping_tolerance = 15.f;
	// tracks which columns were seen in the current flam chord
	// this is essentially the same as if NoteInfo[row].notes
	// was tracked over multiple rows
	int cols = 0;
	// all permutations of these values are unique identifiers
	int col_id[4] = { 1, 2, 4, 8 };
	// unused atm but we might want this information, allocate once
	vector<int> flam_rows(4);
	// timing points of the elements of the flam chord, allocate once
	vector<float> flamjam(4);
	// we don't actually need this counter since we can derive it from cols but
	// it might just be faster to track it locally since we will be recycling
	// the flamjam vector memory
	int flam_row_counter = 0;
	bool flamjamslamwham = false;

	// in each interval
	for (size_t i = 0; i < nervIntervals.size(); i++) {
		// build up flam detection for this interval
		vector<float> temp_mod;

		// row loop to pick up flams within the interval
		for (int row : nervIntervals[i]) {
			// perhaps we should start tracking this instead of tracking it over
			// and over....
			float scaled_time = NoteInfo[row].rowTime / music_rate * 1000.f;

			// this can be optimized a lot by properly mapping out the notes
			// value to arrow combinations (as it is constructed from them) and
			// deterministic

			// we are traversing intervals->rows->columns
			for (auto& id : col_id) {
				// check if there's a note here
				bool isnoteatcol = NoteInfo[row].notes & id;
				if (isnoteatcol) {
					// we're past the tolerance range, break if we have grouped
					// more than 1 note, or if we have filled an entire quad.
					// with this behavior if we fill a quad of 192nd flams with
					// order 1234 and there's still another note on 1 within the
					// tolerance range we'll flag this as a flam chord and
					// downscale appropriately, not sure if we want this as it
					// could be the case that there is a second flamchord
					// immediately after, and it's just vibro, or it could be
					// the case that there are complex reasonable patterns
					// following, perhaps a different behavior would be better

					// we cannot exceed tolerance without at least 1 note
					bool tol_exceed =
					  flam_row_counter > 0 &&
					  (scaled_time - flamjam[0]) > grouping_tolerance;

					if (tol_exceed && flam_row_counter == 1) {
						// single note, don't flag a detect
						flamjamslamwham = false;

						// reset
						flam_row_counter = 0;
						cols = 0;
					}
					if ((tol_exceed && flam_row_counter > 1) ||
						flam_row_counter == 4)
						// at least a flam jump has been detected, flag it
						flamjamslamwham = true;

					// if we have identified a flam chord in some way; handle
					// and reset, we don't want to skip the notes in this
					// iteration yes this should be done in the column loop
					// since a flam can start and end on any columns in any
					// order

					// conditions to be here are at least 2 different columns
					// have been logged as part of a flam chord and we have
					// exceeded the tolerance for flam duration, or we have a
					// full quad flam detected, though not necessarily exceeding
					// the tolerance window. we do want to reset if it doesn't,
					// because we want to pick up vibro flams and nerf them into
					// oblivion too, i think
					if (flamjamslamwham) {
						// we'll construct the final pattern mod value from the
						// flammyness and number of detected flam chords
						float mod_part = 0.f;

						// lower means more cheesable means nerf harder
						float fc_dur =
						  flamjam[flam_row_counter - 1] - flamjam[0];

						// we don't want to affect explicit chords, but we have
						// to be sure that the entire flam we've picked up is an
						// actual chord and only an actual chord, if the first
						// and last elements detected were on the same row,
						// ignore it, trying to do fc_dur == 0.f didn't work
						// because of float precision
						if (flam_rows[0] != flam_rows[flam_row_counter - 1]) {
							// basic linear scale for testing purposes, scaled
							// to the window length and also flam size
							mod_part =
							  fc_dur / grouping_tolerance / flam_row_counter;
							temp_mod.push_back(mod_part);
						}

						// reset
						flam_row_counter = 0;
						cols = 0;
						flamjamslamwham = false;
					}

					// we know chord flams can't contain multiple notes of the
					// same column (those are just gluts), reset if detected
					// even within the tolerance range (we can't be outside of
					// it here by definition)
					if (cols & id) {
						flamjamslamwham = false;

						// reset
						flam_row_counter = 0;
						cols = 0;
					}

					// conditions to reach here are that a note in this column
					// has not been logged yet and we are still within the
					// grouping tolerance. we don't need cur/last times here,
					// the time of the first element will be used to determine
					// the size of the total group

					// track the time point of this note
					flamjam[flam_row_counter] = scaled_time;
					// track which row its on
					flam_rows[flam_row_counter] = row;

					// update unique column identifier
					cols += id;
					++flam_row_counter;
				}
			}
		}

		// finishing the row loop leaves us with instances of flamjams
		// forgive a single instance of a chord flam for now; handle none
		if (temp_mod.size() < 2)
			doot[FlamJam][i] = 1.f;

		float wee = 0.f;
		for (auto& v : temp_mod)
			wee += v;

		// we can do this for now without worring about /0 since size is at
		// least 2 to get here
		wee /= static_cast<float>(temp_mod.size() - 1);

		wee = CalcClamp(1.f - wee, 0.5f, 1.f);
		doot[FlamJam][i] = wee;

		// reset the stuffs, _theoretically_ since we are sequencing we don't
		// even need at all to clear the flam detection however then we have
		// to handle cases like a single note in an interval and i don't feel
		// like doing that, a small number of flams that happen to straddle
		// the interval splice points shouldn't make a huge difference, but if
		// they do then we should deal with it
		temp_mod.clear();
		flam_row_counter = 0;
		cols = 0;
	}
	if (SmoothPatterns)
		Smooth(doot[FlamJam], 1.f);
}

// since the calc skillset balance now operates on +- rather than just - and
// then normalization, we will use this to depress the stream rating for
// non-stream files. edit: ok technically this should be done in the sequential
// pass however that's getting so bloated and efficiency has been optimized
// enough we can just loop through noteinfo sequentially a few times and it's
// whatever

// the chaos mod is also determined here for the moment, which pushes up polys
// and stuff... idk how it even works myself tbh its a pretty hackjobjob
void
Calc::SetStreamMod(const vector<NoteInfo>& NoteInfo,
				   vector<float> doot[ModCount],
				   float music_rate)
{
	doot[StreamMod].resize(nervIntervals.size());
	float lasttime = -1.f;
	for (size_t i = 0; i < nervIntervals.size(); i++) {
		unsigned int taps = 0;
		unsigned int singletaps = 0;
		set<float> whatwhat;
		vector<float> whatwhat2;
		for (int row : nervIntervals[i]) {
			unsigned int notes = column_count(NoteInfo[row].notes);
			taps += notes;
			if (notes == 1)
				++singletaps;

			float curtime = NoteInfo[row].rowTime / music_rate;

			float giraffeasaurus = curtime - lasttime;
			// screen out large hits, it should be ok if this is a discrete
			// cutoff, but i don't like it
			if (giraffeasaurus < 0.25f)
				whatwhat.emplace(giraffeasaurus);

			// instead of making another new mod, calculate the original and
			// most basic chaos mod and apply it along with the new one
			for (size_t i = 0; i < notes; ++i)
				whatwhat2.push_back(giraffeasaurus);
			lasttime = curtime;
		}

		auto HE = [](float x) {
			static const int HE = 9;
			int this_is_a_counter = 0;
			vector<float> o(2 * (HE - 2) + 1);
			for (int i = 2; i < HE; ++i) {
				o[this_is_a_counter] = (1000.f / i * static_cast<float>(x));
				++this_is_a_counter;
			}
			o[this_is_a_counter] = 1000.f * static_cast<float>(x);
			++this_is_a_counter;
			for (int i = 2; i < HE; ++i) {
				o[this_is_a_counter] = (1000.f * i * static_cast<float>(x));
				++this_is_a_counter;
			}
			return o;
		};
		vector<vector<float>> hmmk;
		for (auto& e : whatwhat)
			hmmk.emplace_back(HE(e));

		// I'M SURE THERE'S AN EASIER/FASTER WAY TO DO THIS
		float stub = 0.f;
		// compare each expanded sequence with every other
		if (hmmk.size() > 1) {
			vector<float> mmbop;
			set<int> uniqshare;
			vector<float> biffs;
			vector<float> awwoo;
			for (size_t i = 0; i < hmmk.size() - 1; ++i) {
				float zop = 0.f;
				auto& a = hmmk[i];
				// compare element i against all others
				for (size_t j = i + 1; j < hmmk.size(); ++j) {
					auto& b = hmmk[j]; // i + 1 - last
					biffs.clear();
					for (size_t pP = 0; pP < a.size(); ++pP) {
						for (size_t vi = 0; vi < a.size(); ++vi) {
							float hi = 0.f;
							float lo = 0.f;
							if (a[pP] > b[vi]) {
								hi = a[pP];
								lo = b[vi];
							} else {
								lo = a[pP];
								hi = b[vi];
							}
							biffs.emplace_back(fastsqrt(hi / lo));
						}
					}

					// not exactly correct naming but basically if hi/lo is
					// close enough to 1 we can consider the two points an
					// intersection between the respective quantization waves,
					// the more intersections we pick up and the closer they are
					// to 1 the more confident we are that what we have are
					// duplicate quantizations, and the lower the final mod is
					int under1 = 0;
					float hair_scrunchy = 0.f;
					for (auto& lul : biffs) {
						if (lul < 1.05f) {
							++under1;
							// inverting; 1.05 values should produce a lower mod
							// than 1.0s and since we are using this value as a
							// divisor we need to flip it around
							hair_scrunchy += 2.f - lul;
						}
					}
					awwoo.clear();
					for (auto& lul : biffs)
						awwoo.emplace_back(
						  1.f / static_cast<float>(hair_scrunchy + 1.f));
					uniqshare.insert(under1);
					// std::cout << "shared: " << under1 << std::endl;
				}
				zop = mean(awwoo);
				mmbop.push_back(zop);
				// std::cout << "zope: " << zop << std::endl;
			}
			stub = mean(mmbop);
			stub *= fastsqrt(static_cast<float>(uniqshare.size()));
			// std::cout << "mmbop: " << stub << std::endl;

			stub += 0.9f;
			float test_chaos_merge_stuff = sqrt(0.9f + cv(whatwhat2));
			test_chaos_merge_stuff =
			  CalcClamp(test_chaos_merge_stuff, 0.975f, 1.025f);
			stub =
			  CalcClamp(fastsqrt(stub) * test_chaos_merge_stuff, 0.975f, 1.025f);
			// std::cout << "uniq " << uniqshare.size() << std::endl;
		} else {
			// can't compare if there's only 1 ms value
			stub = 1.f;
		}

		// 1 tap is by definition a single tap
		if (taps < 2) {
			doot[StreamMod][i] = 1.f;
			doot[Chaos][i] = stub;
		} else if (singletaps == 0) {
			doot[StreamMod][i] = 0.8f;
			doot[Chaos][i] = stub;
		} else {
			// we're going to use this to downscale the stream skillset of
			// anything that isn't stream, just a simple tap proportion for the
			// moment but maybe if we need to do fancier sequential stuff we
			// can, the only real concern are jack files registering as stream
			// and that shouldn't be an issue because the amount of single taps
			// required to do that to any effectual level would be unplayable

			// we could also use this to push up stream files if we wanted to
			// but i don't think that's advisable or necessary

			// we want very light js to register as stream, something like jumps
			// on every other 4th, so 17/19 ratio should return full points, but
			// maybe we should allow for some leeway in bad interval slicing
			// this maybe doesn't need to be so severe, on the other hand, maybe
			// it doesn'ting need to be not needing'nt to be so severe
			float prop = static_cast<float>(singletaps + 1) /
						 static_cast<float>(taps - 1) * 10.f / 7.f;
			doot[StreamMod][i] = CalcClamp(fastsqrt(prop), 0.8f, 1.0f);
			doot[Chaos][i] = stub;
		}
	}
	for (auto& v : doot[Chaos])
		if (debugmode) {
			// std::cout << "butts: final " << v << std::endl;
		}
	if (SmoothPatterns) {
		Smooth(doot[StreamMod], 1.f);
		Smooth(doot[Chaos], 1.f);
		Smooth(doot[Chaos], 1.f);
	}
}

// downscales full rolls or rolly js, it looks explicitly for consistent
// cross column timings on both hands; consecutive notes on the same column
// will reduce the penalty 0.5-1 multiplier also now downscales ohj because
// we don't want to run this loop too often even if it makes code slightly
// clearer, i think, new ohj scaler is the same as the last one but gives
// higher weight to sequences of ohjumps 0.5-1 multipier
void
Calc::SetSequentialDownscalers(const vector<NoteInfo>& NoteInfo,
							   unsigned int t1,
							   unsigned int t2,
							   float music_rate,
							   vector<float> doot[ModCount])
{
	doot[Roll].resize(nervIntervals.size());
	doot[OHJump].resize(nervIntervals.size());
	doot[OHTrill].resize(nervIntervals.size());
	doot[Chaos].resize(nervIntervals.size());

	vector<float> wadf1;
	vector<float> wadf2;
	vector<float> wadf3;
	vector<float> wadf4;

	// not sure if these should persist between intervals or not
	// not doing so makes the pattern detection slightly more strict
	// doing so will give the calc some context from the previous
	// interval but might have strange practical consequences
	// another major benefit of retaining last col from the previous
	// interval is we don't have to keep resetting it and i don't like
	// how that case is handled atm

	vector<float> lr;
	vector<float> rl;
	float lasttime = 0.f;
	float dswip = 0.f;
	int lastcol = -1;
	int lastsinglecol = -1;
	static const float water_it_for_me = 0.05f;
	for (size_t i = 0; i < nervIntervals.size(); i++) {
		wadf1 = wadf2;
		wadf2 = wadf3;
		wadf3 = wadf4;
		wadf4.clear();

		// roll downscaler stuff
		// this appears not to be picking up certain patterns in certain
		// test files, reminder to investigate
		int totaltaps = 0;

		lr.clear();
		rl.clear();

		int ltaps = 0;
		int rtaps = 0;
		int dswap = 0;

		// ohj downscaler stuff
		int jumptaps = 0;		// more intuitive to count taps in jumps
		int maxseqjumptaps = 0; // basically the biggest sequence of ohj
		float ohj = 0.f;
		if (debugmode)
			std::cout << "new interval" << std::endl;
		for (int row : nervIntervals[i]) {
			if (debugmode)
				std::cout << "new row" << std::endl;
			bool lcol = NoteInfo[row].notes & t1;
			bool rcol = NoteInfo[row].notes & t2;
			totaltaps += (static_cast<int>(lcol) + static_cast<int>(rcol));
			float curtime = NoteInfo[row].rowTime / music_rate;

			wadf4.push_back(curtime - lasttime);

			// as variation approaches 0 the effect of variation diminishes,
			// e.g. given 140, 140, 120 ms and 40, 40, 20 ms the variation in
			// 40, 40, 20 is meaningless since they're all psuedo jumps anyway,
			// but it will prevent the roll downscaler from being applied to the
			// degree it should, so add a flat value to water down the effect
			float bloaaap = water_it_for_me + curtime - lasttime;

			// ignore jumps/no tapsals
			if (!(lcol ^ rcol)) {
				// fully skip empty rows, set nothing
				if (!(lcol || rcol)) {
					if (debugmode)
						std::cout << "empty row" << std::endl;
					continue;
				}

				if (debugmode)
					std::cout << "jump" << std::endl;

				// add jumptaps when hitting jumps for ohj
				// turns out in order to catch rolls with periodic [12] jumps we
				// need to actually count them as taps-inside-rolls rather than
				// just ignoring them, and we can try kicking back an extra
				// value into the lr or rl vectors since 1->[12] is technically
				// a 1->2 and the difference in motion isn't appreciably
				// different under the circumstances we are interested in
				if (lcol && rcol) {
					jumptaps += 2;
					if (lastcol == 0) {
						lr.push_back(bloaaap);
					}
					if (lastcol == 1) {
						rl.push_back(bloaaap);
					}
					lastsinglecol = lastcol;
					// on ohjumps treat the next note as always cross column
					lastcol = -1;
				}

				// yes we want to set this for jumps
				lasttime = curtime;

				// set the largest ohj sequence
				maxseqjumptaps = max(maxseqjumptaps, jumptaps);
				continue;
			}

			// if lcol is true and we are here we have 1 single tap and if lcol
			// is true we are on column 0; don't try to be clever, even if lcol
			// < rcol to convert bools into ints into a bool into an int worked
			// it was needlessly confusing
			int thiscol = lcol ? 0 : 1;

			// ignore consecutive notes, if we encountered a one hand jump treat
			// it as always being a column swap
			// if (debugmode)
			// std::cout << "lastcol is " << lastcol << std::endl;
			// if (debugmode)
			// std::cout << "thiscol is " << thiscol << std::endl;
			if (thiscol != lastcol || lastcol == -1) {
				// treat 1[12]2 as different from 1[12]1, count the latter as an
				// anchor and the former as a roll with 4 notes
				if (lastcol == -1)
					if (lcol) {
						++ltaps;
						++rtaps;
						//++dswap;
					} else {
						++ltaps;
						++rtaps;
					}

				// this is the col we END on, so if we end on right, we are left
				// to right, not right to left
				if (rcol) {
					lr.push_back(bloaaap);
					++ltaps;
					// if (debugmode)
					// std::cout << "left right " << curtime - lasttime
					//		  << std::endl;
				} else if (lcol) {
					rl.push_back(bloaaap);
					++rtaps;
					// if (debugmode)
					// std::cout << "right to left " << curtime - lasttime
					//		  << std::endl;
				} else {
					// if (debugmode)
					// std::cout << "THIS CANT HAPPEN AAAAAAAAAAAAA" <<
					// std::endl;
				}
				// only log cross column lasttimes on single notes
				lasttime = curtime;
			} else {
				// if (debugmode)
				// std::cout << "anchor" << std::endl;
				// consecutive notes should "poison" the current cross
				// column vector but without shifting the proportional
				// scaling too much this is to avoid treating 121212212121
				// too much like 121212121212

				// if we wanted to be _super explicit_ we could just reset
				// the lr/rl vectors when hitting a consecutive note (and/or
				// jump), there are advantages to being hyper explicit but
				// at the moment this does sort of pick up rolly js quite
				// well, though it would probably be more responsible
				// longterm to have an explicit roll detector an explicit
				// trill detector, and an explicit rolly js detector thing
				// is debugging all 3 and making sure they work as intended
				// and in exclusion is just as hard as making a couple of
				// more generic mods and accepting they will overlap in
				// certain scenarios though again on the other hand explicit
				// modifiers are easier to tune you just have to do a lot
				// more of it

				if (rcol)
					lr.push_back(bloaaap);
				else if (lcol)
					rl.push_back(bloaaap);

				// we have an anchor and we either have moderately complex
				// patterning now or we have simply changed direction of the
				// roll
				++dswap;
			}

			// ohj downscaler stuff
			// we know between the following that the latter is more
			// difficult [12][12][12]222[12][12][12]
			// [12][12][12]212[12][12][12]
			// so we want to penalize not only any break in the ohj sequence
			// but further penalize breaks which contain cross column taps
			// this should also reflect the difference between [12]122[12],
			// [12]121[12] cases like 121[12][12]212[12][12]121 should
			// probably have some penalty but likely won't with this setup,
			// but everyone hates that anyway and it would be quite
			// difficult to force the current setup to do so without
			// increasing complexity significantly (probably)
			jumptaps -=
			  1; // we're already on single notes, so just decrement a lil
			if (thiscol != lastcol) // easier to read if we do it again
				jumptaps -= 2;

			lastcol = thiscol;
		}
		if (debugmode) {
			std::string rarp = "left to right: ";

			for (auto& a : lr) {
				rarp.append(std::to_string(a - water_it_for_me));
				rarp.append(", ");
			}
			rarp.append("\nright to left: ");

			for (auto& b : rl) {
				rarp.append(std::to_string(b - water_it_for_me));
				rarp.append(", ");
			}

			std::cout << "" << rarp << std::endl;
		}
		// I DONT KNOW OK
		dswip = (dswip + dswap) / 2.f;
		int cvtaps = ltaps + rtaps;

		// if this is true we have some combination of single notes and
		// jumps where the single notes are all on the same column
		if (cvtaps == 0) {
			// we don't want to treat 2[12][12][12]2222
			// 2222[12][12][12]2 differently, so use the
			// max sequence here exclusively
			if (maxseqjumptaps > 0)
				doot[OHJump][i] =
				  CalcClamp(1.f * static_cast<float>(totaltaps) /
							  (static_cast<float>(maxseqjumptaps) * 2.5f),
							0.5f,
							1.f);
			else // single note longjacks, do nothing
				doot[OHJump][i] = 1.f;

			// no rolls here by definition
			doot[Roll][i] = 0.9f;
			doot[OHTrill][i] = 1.f;

			continue;
		}

		float cvlr = 0.2f;
		float cvrl = 0.2f;
		if (ltaps > 1)
			cvlr = cv(lr);
		if (rtaps > 1)
			cvrl = cv(rl);

		if (debugmode)
			std::cout << "cv lr " << cvlr << std::endl;
		if (debugmode)
			std::cout << "cv rl " << cvrl << std::endl;

		// weighted average, but if one is empty we want it to skew high not
		// low due to * 0
		float Cv = ((cvlr * (ltaps + 1)) + (cvrl * (rtaps + 1))) /
				   static_cast<float>(cvtaps + 2);

		if (debugmode)
			std::cout << "cv " << cv << std::endl;

		// the vector with the higher mean should carry a little more weight

		float yes_trills = 1.f;

		// check for oh trills
		if (true) {
			if (!lr.empty() && !rl.empty()) {

				// ok this is SUPER jank but we added a flat amount to the ms
				// values to water down the effects of variation, but that will
				// negate the differential between the means of the two, so now
				// we have to again subtract that amount from the ms values in
				// the vectors
				for (auto& v : lr)
					v -= water_it_for_me;
				for (auto& v : rl)
					v -= water_it_for_me;

				float no_trills = 1.f;
				float mlr = mean(lr);
				float mrl = mean(rl);
				bool rl_is_higher = mlr < mrl;

				// if the mean of one isn't much higher than the other, it's oh
				// trills, so leave it alone, if it is, scale down the roll
				// modifier by the oh trillyness, we don't want to affect that
				// here
				float div = rl_is_higher ? mrl / mlr : mlr / mrl;
				div = CalcClamp(div, 1.f, 3.f);
				if (debugmode)
					std::cout << "div " << div << std::endl;
				no_trills = CalcClamp(1.75f - div, 0.f, 1.f);

				// store high oh trill detection in case
				// we want to do stuff with it later
				yes_trills = Cv - no_trills;
				yes_trills = CalcClamp(yes_trills, 0.1f, 1.f);
				Cv += no_trills * 1.f; // just straight up add to cv
			}
		}

		// cv = rl_is_higher ? (2.f * cv + cvrl) / 3.f : (2.f * cv + cvlr)
		// / 3.f; if (debugmode) 	std::cout << "cv2 " << cv << std::endl;
		/*



		// then scaled against how many taps we ignored
		
		float barf = (-0.1f + (dswap * 0.1f));
		barf += (barf2 - 1.f);
		if (debugmode)
			std::cout << "barf " << barf << std::endl;
		cv += barf;
		cv *= barf2;
		cv = CalcClamp(cv, 0.f, 1.f);

		if (debugmode)
			std::cout << "cv3 " << cv << std::endl;
		yes_trills *= barf;
		*/

		// we just want a minimum amount of variation to escape getting
		// downscaled cap to 1 (it's not an inherently bad idea to upscale
		// sets of patterns with high variation but we shouldn't do that
		// here, probably)

		vector<float> voobles;
		set<float> bibbles;
		float bbbrap = 0.f;
		if (i > 4) {
			voobles.clear();
			for (auto& aaa : wadf1) {
				// voobles.push_back(aaa);
				// bibbles.insert(aaa);
			}
			for (auto& aaa : wadf2) {
				//voobles.push_back(aaa);
				//bibbles.insert(aaa);
			}
			for (auto& aaa : wadf3) {
				voobles.push_back(aaa);
				bibbles.insert(aaa);
			}
			for (auto& aaa : wadf4) {
				voobles.push_back(aaa);
				bibbles.insert(aaa);
			}
			bbbrap = pow(cv(voobles), 3.f) *
					 (10.f * static_cast<float>(bibbles.size()) /
					  static_cast<float>(voobles.size() + 1));
			bbbrap += 0.55f;
			bbbrap = CalcClamp(bbbrap, 0.4f, 2.f);

			if (debugmode) {
				std::string rarp = "voobles: ";
				for (auto& a : voobles) {
					rarp.append(std::to_string(a));
					rarp.append(", ");
				}
				std::cout << rarp << std::endl;
				std::cout << "bibbles " << bibbles.size() / voobles.size()
						  << std::endl;
			}
		} else {
			bbbrap = 1.f;
		}

		float barf2 =
		  static_cast<float>(totaltaps) / static_cast<float>(cvtaps);
		float barf = 0.25f +
		  0.4f * (static_cast<float>(totaltaps) / static_cast<float>(cvtaps)) +
		  dswip * 0.25f;
		Cv = sqrt(Cv) - 0.1f;
		Cv += barf;
		Cv *= barf2;
		//bbbrap = CalcClamp(bbbrap, Cv, 2.f);
		Cv *= bbbrap;
		doot[Roll][i] = CalcClamp(Cv, 0.5f, 1.f);

		doot[OHTrill][i] = bbbrap;
		// CalcClamp(0.5f + fastsqrt(yes_trills), 0.8f, 1.f);
		if (debugmode)
			std::cout << "final mod " << doot[Roll][i] << "\n" << std::endl;
		// ohj stuff, wip
		if (jumptaps < 1 && maxseqjumptaps < 1)
			doot[OHJump][i] = 1.f;
		else {
			ohj = static_cast<float>(maxseqjumptaps + 1) /
				  static_cast<float>(totaltaps + 1);
			doot[OHJump][i] = CalcClamp(0.5f + fastsqrt(ohj), 0.5f, 1.f);
		}
	}

	if (SmoothPatterns) {
		Smooth(doot[Roll], 1.f);
		Smooth(doot[Roll], 1.f);
		Smooth(doot[OHJump], 1.f);
	}

	// this is fugly but basically we want to negate any _bonus_ from chaos if
	// the polys are arranged in a giant ass roll formation
	for (size_t i = 0; i < doot[Chaos].size(); ++i)
		doot[Chaos][i] = CalcClamp(doot[Chaos][i] * doot[Roll][i],
								   doot[Chaos][i],
								   max(doot[Chaos][i] * doot[Roll][i], 1.f));

	return;
}

static const float ssr_goal_cap = 0.965f; // goal cap to prevent insane scaling

// Function to generate SSR rating
vector<float>
MinaSDCalc(const vector<NoteInfo>& NoteInfo, float musicrate, float goal)
{
	if (NoteInfo.size() <= 1)
		return { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
	return std::make_unique<Calc>()->CalcMain(
	  NoteInfo, musicrate, min(goal, ssr_goal_cap));
}

// Wrap difficulty calculation for all standard rates
MinaSD
MinaSDCalc(const vector<NoteInfo>& NoteInfo)
{
	MinaSD allrates;
	int lower_rate = 7;
	int upper_rate = 21;

	if (NoteInfo.size() > 1) {
		std::unique_ptr<Calc> cacheRun = std::make_unique<Calc>();
		cacheRun->capssr = false;
		for (int i = lower_rate; i < upper_rate; i++) {
			allrates.emplace_back(cacheRun->CalcMain(
			  NoteInfo, static_cast<float>(i) / 10.f, 0.93f));
		}
	}

	else {
		vector<float> output{ 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
		for (int i = lower_rate; i < upper_rate; i++)
			allrates.emplace_back(output);
	}
	return allrates;
}

// Debug output
void
MinaSDCalcDebug(const vector<NoteInfo>& NoteInfo,
				float musicrate,
				float goal,
				vector<vector<vector<vector<float>>>>& handInfo)
{
	if (NoteInfo.size() <= 1)
		return;

	std::unique_ptr<Calc> debugRun = std::make_unique<Calc>();
	debugRun->debugmode = true;
	debugRun->CalcMain(NoteInfo, musicrate, min(goal, ssr_goal_cap));

	handInfo.emplace_back(debugRun->left_hand.debugValues);
	handInfo.emplace_back(debugRun->right_hand.debugValues);
}

int
GetCalcVersion()
{
	return 279;
}
