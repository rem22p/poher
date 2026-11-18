#include "bots/style.hpp"

#include <utility>

namespace poher::bots {

namespace {

std::vector<Character> make() {
    std::vector<Character> all;

    // Calls almost anything, rarely raises, believes in any pair.
    Style vasya;
    vasya.loose = 1.9;
    vasya.fancy = 0.6;
    vasya.pre_raise = 0.12;
    vasya.aggr = 0.15;
    vasya.bluff = 0.04;
    vasya.sticky = 0.75;
    vasya.fear = 0.05;
    vasya.trap = 0.1;
    vasya.chase = 0.7;
    vasya.tilt = 0.2;
    vasya.guard = 0;
    vasya.read = 0.65;
    vasya.noise = 0.07;
    vasya.adapt = 0;
    vasya.size = 0.7;
    vasya.honest = 0.5;
    vasya.tempo = 0.8;
    vasya.talk = 0.8;
    all.push_back({"Vasya", {vasya}});

    // Bets and raises with anything, bluffs a lot, tilts easily.
    Style chad;
    chad.loose = 1.6;
    chad.fancy = 0.5;
    chad.pre_raise = 0.85;
    chad.aggr = 0.85;
    chad.bluff = 0.33;
    chad.sticky = 0.45;
    chad.fear = 0;
    chad.trap = 0;
    chad.chase = 0.5;
    chad.tilt = 0.9;
    chad.guard = 0;
    chad.read = 0.5;
    chad.noise = 0.08;
    chad.adapt = 0;
    chad.size = 1.35;
    chad.honest = 0.1;
    chad.tempo = 0.65;
    chad.talk = 1;
    all.push_back({"Chad", {chad}});

    // Plays only strong cards and is scared of big bets.
    Style bibi;
    bibi.loose = 0.6;
    bibi.fancy = 0;
    bibi.pre_raise = 0.55;
    bibi.aggr = 0.3;
    bibi.bluff = 0.02;
    bibi.sticky = 0.1;
    bibi.fear = 0.9;
    bibi.trap = 0.1;
    bibi.chase = 0.1;
    bibi.tilt = 0.1;
    bibi.guard = 0.6;
    bibi.read = 0.55;
    bibi.noise = 0.06;
    bibi.adapt = 0;
    bibi.size = 0.8;
    bibi.honest = 0.8;
    bibi.tempo = 1.3;
    bibi.talk = 0.5;
    all.push_back({"Bibi", {bibi}});

    // Few hands, played hard. Honest and predictable.
    Style niga;
    niga.loose = 0.75;
    niga.fancy = 0;
    niga.pre_raise = 0.95;
    niga.aggr = 0.7;
    niga.bluff = 0.06;
    niga.sticky = 0.2;
    niga.fear = 0.3;
    niga.trap = 0.05;
    niga.chase = 0.2;
    niga.tilt = 0.15;
    niga.guard = 0.3;
    niga.read = 0.75;
    niga.noise = 0.045;
    niga.adapt = 0.3;
    niga.size = 1;
    niga.honest = 0.9;
    niga.tempo = 1;
    niga.talk = 0.3;
    all.push_back({"Niga", {niga}});

    // Likes traps: hides a strong hand, then raises sharply.
    Style karp;
    karp.loose = 0.95;
    karp.fancy = 0.1;
    karp.pre_raise = 0.7;
    karp.aggr = 0.5;
    karp.bluff = 0.12;
    karp.sticky = 0.3;
    karp.fear = 0.2;
    karp.trap = 0.75;
    karp.chase = 0.3;
    karp.tilt = 0.2;
    karp.guard = 0.2;
    karp.read = 0.8;
    karp.noise = 0.04;
    karp.adapt = 0.4;
    karp.size = 1;
    karp.honest = 0.2;
    karp.tempo = 1.15;
    karp.talk = 0.5;
    all.push_back({"Karp", {karp}});

    // Plays many hands, chases draws, steams after a big loss.
    Style snegr;
    snegr.loose = 1.45;
    snegr.fancy = 0.4;
    snegr.pre_raise = 0.5;
    snegr.aggr = 0.55;
    snegr.bluff = 0.2;
    snegr.sticky = 0.45;
    snegr.fear = 0.1;
    snegr.trap = 0.1;
    snegr.chase = 0.8;
    snegr.tilt = 0.8;
    snegr.guard = 0;
    snegr.read = 0.85;
    snegr.noise = 0.05;
    snegr.adapt = 0.3;
    snegr.size = 1;
    snegr.honest = 0.4;
    snegr.tempo = 0.9;
    snegr.talk = 0.9;
    all.push_back({"Snegr", {snegr}});

    // Counts the odds, bluffs rarely and with a reason.
    Style chips;
    chips.loose = 1;
    chips.fancy = 0;
    chips.pre_raise = 1;
    chips.aggr = 0.65;
    chips.bluff = 0.12;
    chips.sticky = 0.15;
    chips.fear = 0.1;
    chips.trap = 0.2;
    chips.chase = 0.25;
    chips.tilt = 0.05;
    chips.guard = 0.1;
    chips.read = 1;
    chips.noise = 0.02;
    chips.adapt = 0.7;
    chips.size = 0.95;
    chips.honest = 0;
    chips.tempo = 1.1;
    chips.talk = 0.4;
    all.push_back({"Chips", {chips}});

    // Leans on everybody with bets, bluffs in good spots, adjusts fast.
    Style slon;
    slon.loose = 1.25;
    slon.fancy = 0.1;
    slon.pre_raise = 1;
    slon.aggr = 0.85;
    slon.bluff = 0.28;
    slon.sticky = 0.2;
    slon.fear = 0.05;
    slon.trap = 0.15;
    slon.chase = 0.35;
    slon.tilt = 0.15;
    slon.guard = 0;
    slon.read = 1;
    slon.noise = 0.025;
    slon.adapt = 1;
    slon.size = 1.15;
    slon.honest = 0;
    slon.tempo = 0.9;
    slon.talk = 0.5;
    all.push_back({"Slon", {slon}});

    // Says nothing, changes style as the game goes, hard to read.
    Style vlid;
    vlid.fancy = 0;
    vlid.pre_raise = 0.95;
    vlid.sticky = 0.2;
    vlid.fear = 0.1;
    vlid.chase = 0.3;
    vlid.tilt = 0.05;
    vlid.guard = 0.1;
    vlid.read = 1;
    vlid.noise = 0.02;
    vlid.adapt = 0.8;
    vlid.honest = 0;
    vlid.tempo = 1;
    vlid.talk = 0;
    Style tight = vlid;   // few hands, straightforward pressure
    tight.loose = 0.8;
    tight.aggr = 0.7;
    tight.bluff = 0.1;
    tight.trap = 0.15;
    Style wild = vlid;    // many hands, bets everywhere
    wild.loose = 1.45;
    wild.aggr = 0.85;
    wild.bluff = 0.3;
    wild.trap = 0.1;
    wild.size = 1.15;
    Style sly = vlid;     // sits back and springs traps
    sly.loose = 1.05;
    sly.aggr = 0.45;
    sly.bluff = 0.08;
    sly.trap = 0.6;
    sly.pre_raise = 0.75;
    all.push_back({"Vlid", {tight, wild, sly}, 20, 45});

    return all;
}

std::vector<Character>& all_characters() {
    static std::vector<Character> all = make();
    return all;
}

}  // namespace

const std::vector<Character>& characters() {
    return all_characters();
}

const Character& character(std::string_view name) {
    for (const Character& who : characters()) {
        if (who.name == name) return who;
    }
    static const Character plain{"", {Style{}}};
    return plain;
}

void put_character(Character who) {
    for (Character& known : all_characters()) {
        if (known.name == who.name) {
            known = std::move(who);
            return;
        }
    }
    all_characters().push_back(std::move(who));
}

}  // namespace poher::bots
