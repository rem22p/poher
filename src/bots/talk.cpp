#include "bots/talk.hpp"

namespace poher::bots {

const std::vector<Phrase>& phrases() {
    using enum Occasion;
    static const std::vector<Phrase> all = {
        // Vasya: cheerful, never sure what happened.
        {"Vasya", Hello, "hi all, good luck", "всем привет, удачи"},
        {"Vasya", WonBig, "told you it was good!", "я ж говорил, зайдёт!"},
        {"Vasya", WonBig, "now that's a card", "вот это карта пошла"},
        {"Vasya", LostBig, "ah well, next one", "да и ладно, следующая"},
        {"Vasya", LostBig, "how do you always have it", "ну как у тебя всегда есть"},
        {"Vasya", BadBeat, "nooo, not the river", "ну ривер, ну зачем"},
        {"Vasya", Bluffed, "hehe", "хе-хе"},
        {"Vasya", NiceHand, "nice one!", "красиво!"},

        // Chad: loud, and it is never his fault.
        {"Chad", Hello, "so who's paying tonight?", "ну что, кто сегодня платит?"},
        {"Chad", WonBig, "pay the man", "касса!"},
        {"Chad", WonBig, "too easy", "слишком просто"},
        {"Chad", LostBig, "are you kidding me", "да вы издеваетесь"},
        {"Chad", LostBig, "what a circus", "ну и раздача, цирк"},
        {"Chad", BadBeat, "you call with THAT??", "ты с ЭТИМ коллируешь??"},
        {"Chad", Bluffed, "should have called ;)", "а надо было коллировать ;)"},
        {"Chad", Bluffed, "I had nothing, by the way", "у меня там было пусто, кстати"},
        {"Chad", NiceHand, "lucky", "повезло"},

        // Bibi: polite and easily frightened.
        {"Bibi", Hello, "hello everyone", "всем здравствуйте"},
        {"Bibi", WonBig, "oh! thank you", "ой, спасибо"},
        {"Bibi", LostBig, "oh no...", "ой-ой..."},
        {"Bibi", LostBig, "I knew I should have folded", "так и знала, надо было сбросить"},
        {"Bibi", BadBeat, "that's not fair", "так нечестно"},
        {"Bibi", NiceHand, "well played", "хорошо сыграно"},

        // Niga: a few plain words.
        {"Niga", Hello, "gl", "удачи"},
        {"Niga", WonBig, "gg", "гг"},
        {"Niga", LostBig, "nh", "хорошая рука"},
        {"Niga", BadBeat, "ok.", "ладно."},
        {"Niga", NiceHand, "nh", "нх"},

        // Karp: sly, pleased with himself.
        {"Karp", Hello, "good evening, gentlemen", "добрый вечер, господа"},
        {"Karp", WonBig, "thank you for betting it for me", "спасибо, что поставили за меня"},
        {"Karp", WonBig, "patience pays", "терпение окупается"},
        {"Karp", LostBig, "noted", "запомню"},
        {"Karp", BadBeat, "a curious call", "любопытный колл"},
        {"Karp", Bluffed, "sometimes I do have it", "иногда у меня что-то есть"},
        {"Karp", NiceHand, "not bad", "недурно"},

        // Snegr: lives every hand out loud.
        {"Snegr", Hello, "gl hf all", "всем гл хф"},
        {"Snegr", WonBig, "LET'S GOOO", "ЕЕЕ, ЗАШЛО"},
        {"Snegr", WonBig, "finally it came in", "наконец-то доехало"},
        {"Snegr", LostBig, "every single time", "да сколько можно"},
        {"Snegr", LostBig, "one card. I needed one card", "одной карты не хватило, одной!"},
        {"Snegr", BadBeat, "unreal", "это просто нереально"},
        {"Snegr", Bluffed, "ez", "изи"},
        {"Snegr", NiceHand, "sick hand", "жёстко"},

        // Chips: everything is a number.
        {"Chips", Hello, "good luck, all", "всем удачи"},
        {"Chips", WonBig, "the odds were there", "шансы были на моей стороне"},
        {"Chips", LostBig, "right call, wrong card", "колл верный, карта не та"},
        {"Chips", BadBeat, "variance", "дисперсия"},
        {"Chips", Bluffed, "good fold", "хороший фолд"},
        {"Chips", NiceHand, "nh", "хорошая рука"},

        // Slon: short, pushing.
        {"Slon", Hello, "let's go", "погнали"},
        {"Slon", WonBig, "next", "дальше"},
        {"Slon", LostBig, "again", "ещё раз"},
        {"Slon", BadBeat, "sure, sure", "ну-ну"},
        {"Slon", Bluffed, "fold more often", "сбрасывай почаще"},
        {"Slon", Bluffed, "keep folding", "так и сбрасывай"},
        {"Slon", NiceHand, "fine, nice", "неплохо"},

        // Vlid says nothing.
    };
    return all;
}

int pick_phrase(std::string_view who, Occasion occasion, game::Random& random) {
    const std::vector<Phrase>& all = phrases();
    int count = 0;
    for (const Phrase& phrase : all) {
        if (phrase.who == who && phrase.occasion == occasion) ++count;
    }
    if (count == 0) return -1;
    int pick = static_cast<int>(random.below(static_cast<std::uint32_t>(count)));
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all[i].who == who && all[i].occasion == occasion && pick-- == 0) return static_cast<int>(i);
    }
    return -1;
}

std::string_view phrase_text(int number, bool russian) {
    const std::vector<Phrase>& all = phrases();
    if (number < 0 || static_cast<std::size_t>(number) >= all.size()) return {};
    return russian ? all[static_cast<std::size_t>(number)].ru : all[static_cast<std::size_t>(number)].en;
}

}  // namespace poher::bots
