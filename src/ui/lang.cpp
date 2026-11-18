#include "ui/lang.hpp"

#include <cstdlib>

#include "ui/card_art.hpp"

#ifdef _WIN32
#include <windows.h>
#endif
#include "bots/talk.hpp"

namespace poher::ui {

namespace {

constexpr Words kEnglish = {
    .language = "English",
    .you = "You",

    .game = "No-Limit Hold'em",
    .blinds = "blinds",
    .hand = "hand #",
    .bankroll = "bankroll",
    .pot = "pot",
    .your_move = "YOUR MOVE",
    .thinking = "thinking…",
    .clock_left = "{} s",
    .clock_reserve = "reserve {} s",
    .offline = "no connection",
    .away_prompt = "You missed your moves and sit out. Press any key to play on.",
    .waiting_for_players = "waiting for players",
    .next_hand_soon = "next hand in a moment",

    .note_waiting = "waiting",
    .note_away = "away",
    .note_small_blind = "SB {}",
    .note_big_blind = "BB {}",
    .note_fold = "fold",
    .note_check = "check",
    .note_call = "call {}",
    .note_bet = "bet {}",
    .note_raise = "raise {}",
    .note_all_in = "all-in {}",
    .note_muck = "mucks",
    .note_wins = "takes the pot",

    .fold = "Fold",
    .check = "Check",
    .call = "Call {}",
    .bet = "Bet",
    .raise = "Raise",
    .bet_to = "Bet {}",
    .raise_to = "Raise to {}",
    .all_in = "All-in {}",
    .size_half = "1/2",
    .size_two_thirds = "2/3",
    .size_pot = "pot",
    .size_all_in = "all-in",
    .raise_keys = "←→ amount    ↑↓ size    0-9 type it    Enter confirm    Esc back",
    .raise_limits = "from {} to {}",
    .out_of_chips = "You are out of chips",
    .buy_in_key = "buy in for {}",
    .refill_key = "refill the bankroll to {} and buy in",
    .chips_coming = "chips will be added after this hand",
    .table_keys = "Tab panel    B add chips    S statistics    H history    Esc menu",
    .panel_log = "this hand",

    .wins = "{} wins {}",
    .split = "split pot: {}",

    .log_hand = "hand #{}",
    .log_blind = "posts {}",
    .log_fold = "folds",
    .log_check = "checks",
    .log_call = "calls {}",
    .log_bet = "bets {}",
    .log_raise = "raises to {}",
    .log_all_in = "is all-in for {}",
    .log_shows = "shows {}",
    .log_mucks = "mucks",
    .log_returned = "gets {} back",
    .log_wins = "wins {}",
    .log_rebuy = "adds {} chips",
    .log_says = "“{}”",

    .menu_play = "Play with bots",
    .menu_refill = "Refill the bankroll to {}",
    .menu_profile = "Profile",
    .menu_history = "Hand history",
    .menu_settings = "Settings",
    .menu_quit = "Quit",
    .menu_keys = "↑↓ choose    Enter open",
    .setup_title = "Table with bots",
    .setup_stakes = "Stakes",
    .setup_seats = "Seats",
    .setup_buy_in = "Buy-in",
    .setup_bots = "Bots",
    .setup_start = "Start",
    .setup_range = "from {} to {}",
    .setup_keys = "↑↓ choose    ←→ change    Space seat a bot    Enter start    Esc back",
    .setup_need_bankroll = "needs a bankroll of {}",
    .setup_not_here = "does not play this high",
    .setup_pick_bots = "seat from 1 to {} bots",
    .level_names = {"Micro", "Low", "Medium", "High", "Very high"},
    .strength_names = {"weak", "medium", "strong"},
    .settings_title = "Settings",
    .settings_language = "Language",
    .settings_hint = "Hand hint",
    .settings_sound = "Sound on your move",
    .settings_talk = "Bots talk",
    .settings_keys = "↑↓ choose    ←→ change    Esc back",
    .hint_off = "off",
    .hint_name = "name of the hand",
    .hint_full = "name, outs and chances",
    .hint_outs = "outs: {} · improves {}%",
    .hint_win = "wins {}%",
    .on = "on",
    .off = "off",
    .on_many = "on",
    .off_many = "off",
    .back = "Back",
    .pause_title = "Menu",
    .pause_resume = "Back to the game",
    .pause_leave = "Leave the table",
    .pause_refill = "Refill the bankroll to {}",
    .pause_quit = "Quit the game",
    .refilled = "bankroll refilled to {}",
    .profile_title = "Profile",
    .profile_nick = "Nick",
    .profile_no_nick = "not set",
    .profile_avatar = "Avatar",
    .profile_bankroll = "Bankroll",
    .profile_refills = "Refills",
    .profile_public = "Friends see",
    .profile_private = "Only you see",
    .stat_hands = "Hands played",
    .stat_hands_won = "Hands won",
    .stat_biggest_pot = "Biggest pot won",
    .stat_vs_bots = "Against bots",
    .stat_vs_people = "Against people",
    .stat_enters = "You enter the pot",
    .stat_raises = "You raise before the flop",
    .stat_pushes = "Bets per call after the flop",
    .stat_showdowns_won = "Showdowns won",
    .profile_keys = "↑↓ choose    Enter type a nick    ←→ change the avatar    Esc back",
    .profile_nick_keys = "type the nick    Enter done    Esc leave as it was",
    .nick_taken = "that is the name of a bot",
    .setup_bot_account = "{}: bankroll {} · hands {} · result {}",
    .menu_friends = "Friends",
    .pause_friends = "Invite friends",
    .setup_title_online = "Your table",
    .setup_friends = "Friends",
    .setup_open = "may sit down",
    .setup_invited = "by invitation",
    .setup_clock = "Move time",
    .setup_no_clock = "no clock",
    .setup_clock_note = "when friends play",
    .table_of = "{}'s table",
    .table_got_up = "You were got up from the table",
    .table_closed = "The host closed the table",
    .host_gone = "The host is gone; the chips are back in your bankroll",
    .host_lost = "No connection with the host. Waiting {} more",
    .joining = "Taking a seat at {}'s table…",
    .joining_keys = "Esc give up",
    .refused_full = "No free seat at that table",
    .refused_closed = "That table is by invitation only",
    .refused_version = "Your friend has another version of POHER: one of you has to update",
    .refused_no_table = "Your friend has no table now",
    .refused_bankroll = "The buy-in does not fit that table",
    .friends_title = "Friends",
    .net_connecting = "connecting to the network…",
    .net_online = "online",
    .my_code = "Your code",
    .my_code_note = "C copies it. Send it to a friend; they add it under \"Add a friend\".",
    .add_friend = "Add a friend by code",
    .code_prompt = "paste the code",
    .request_from = "Request from {}",
    .request_keys = "Enter accept · Backspace decline",
    .friends_none = "No friends yet.",
    .friend_offline = "offline",
    .friend_online = "online",
    .friend_menu = "in the menus",
    .friend_bots = "plays with bots",
    .friend_hosting = "at own table: {} · {} of {}",
    .friend_open = "open",
    .friend_guest = "at a friend's table",
    .friend_version = "other version {}",
    .link_direct = "direct",
    .link_relay = "via relay",
    .friends_keys = "↑↓ choose    Enter join or invite    C copy code    Esc back",
    .code_keys = "paste or type the code    Enter add    Esc cancel",
    .add_sent = "Request sent: your friend sees it once online",
    .add_bad = "That is not a code",
    .add_own = "That is your own code",
    .add_already = "Already a friend",
    .add_failed = "Could not add",
    .code_copied = "Code copied",
    .friend_sat_down = "{} sat down",
    .deck_alarm = "The deck did not match what the host had promised: the cards may have been stacked",
    .deck_doubt = "⚠ deck not fair",
    .invite_sent = "Invitation sent to {}",
    .cannot_join = "Your friend has no open table now",
    .need_bankroll = "That table needs a bankroll of {}",
    .invitation_title = "{} invites you to a table",
    .invitation_table = "{} · {} of {} seats taken",
    .invitation_keys = "Enter sit down (buy-in {})    Esc no",
    .invitation_leave = "You will leave the table you are at now",
    .history_title = "Hand history",
    .history_empty = "No hands played yet.",
    .history_keys = "↑↓ choose    Enter open    Esc back",
    .history_hand_keys = "↑↓ scroll    ←→ next hand    Esc back to the list",
    .history_hand = "hand #{}",
};

constexpr Words kRussian = {
    .language = "Русский",
    .you = "Ты",

    .game = "Безлимитный холдем",
    .blinds = "блайнды",
    .hand = "раздача №",
    .bankroll = "банкролл",
    .pot = "банк",
    .your_move = "ТВОЙ ХОД",
    .thinking = "думает…",
    .clock_left = "{} с",
    .clock_reserve = "запас {} с",
    .offline = "нет связи",
    .away_prompt = "Ты пропустил ходы и пропускаешь раздачи. Нажми любую клавишу, чтобы вернуться.",
    .waiting_for_players = "ждём игроков",
    .next_hand_soon = "сейчас новая раздача",

    .note_waiting = "ждёт",
    .note_away = "отошёл",
    .note_small_blind = "МБ {}",
    .note_big_blind = "ББ {}",
    .note_fold = "фолд",
    .note_check = "чек",
    .note_call = "колл {}",
    .note_bet = "ставка {}",
    .note_raise = "рейз {}",
    .note_all_in = "олл-ин {}",
    .note_muck = "не показал",
    .note_wins = "забирает банк",

    .fold = "Фолд",
    .check = "Чек",
    .call = "Колл {}",
    .bet = "Ставка",
    .raise = "Рейз",
    .bet_to = "Ставка {}",
    .raise_to = "Рейз до {}",
    .all_in = "Олл-ин {}",
    .size_half = "1/2",
    .size_two_thirds = "2/3",
    .size_pot = "банк",
    .size_all_in = "олл-ин",
    .raise_keys = "←→ сумма    ↑↓ размер    0-9 ввести    Enter подтвердить    Esc назад",
    .raise_limits = "от {} до {}",
    .out_of_chips = "Фишки кончились",
    .buy_in_key = "сесть заново за {}",
    .refill_key = "пополнить банкролл до {} и сесть заново",
    .chips_coming = "фишки добавятся после этой раздачи",
    .table_keys = "Tab панель    B докупить фишки    S статистика    H история    Esc меню",
    .panel_log = "эта раздача",

    .wins = "{} выигрывает {}",
    .split = "банк поделён: {}",

    .log_hand = "раздача №{}",
    .log_blind = "блайнд {}",
    .log_fold = "фолд",
    .log_check = "чек",
    .log_call = "колл {}",
    .log_bet = "ставка {}",
    .log_raise = "рейз до {}",
    .log_all_in = "олл-ин {}",
    .log_shows = "показывает {}",
    .log_mucks = "не показывает",
    .log_returned = "забирает {} обратно",
    .log_wins = "выигрывает {}",
    .log_rebuy = "докупает {}",
    .log_says = "«{}»",

    .menu_play = "Играть с ботами",
    .menu_refill = "Пополнить банкролл до {}",
    .menu_profile = "Профиль",
    .menu_history = "История раздач",
    .menu_settings = "Настройки",
    .menu_quit = "Выход",
    .menu_keys = "↑↓ выбор    Enter открыть",
    .setup_title = "Стол с ботами",
    .setup_stakes = "Ставки",
    .setup_seats = "Мест",
    .setup_buy_in = "Вход",
    .setup_bots = "Боты",
    .setup_start = "Начать",
    .setup_range = "от {} до {}",
    .setup_keys = "↑↓ выбор    ←→ изменить    Пробел посадить бота    Enter начать    Esc назад",
    .setup_need_bankroll = "нужен банкролл от {}",
    .setup_not_here = "так высоко не играет",
    .setup_pick_bots = "посади от 1 до {} ботов",
    .level_names = {"Микро", "Низкие", "Средние", "Высокие", "Очень высокие"},
    .strength_names = {"слабый", "средний", "сильный"},
    .settings_title = "Настройки",
    .settings_language = "Язык",
    .settings_hint = "Подсказка руки",
    .settings_sound = "Сигнал на твой ход",
    .settings_talk = "Реплики ботов",
    .settings_keys = "↑↓ выбор    ←→ изменить    Esc назад",
    .hint_off = "выключена",
    .hint_name = "название комбинации",
    .hint_full = "название, ауты и шансы",
    .hint_outs = "ауты: {} · усиление {}%",
    .hint_win = "победа {}%",
    .on = "включён",
    .off = "выключен",
    .on_many = "включены",
    .off_many = "выключены",
    .back = "Назад",
    .pause_title = "Меню",
    .pause_resume = "Вернуться к игре",
    .pause_leave = "Встать из-за стола",
    .pause_refill = "Пополнить банкролл до {}",
    .pause_quit = "Выйти из игры",
    .refilled = "банкролл пополнен до {}",
    .profile_title = "Профиль",
    .profile_nick = "Ник",
    .profile_no_nick = "не задан",
    .profile_avatar = "Аватар",
    .profile_bankroll = "Банкролл",
    .profile_refills = "Пополнений",
    .profile_public = "Видно друзьям",
    .profile_private = "Видно только тебе",
    .stat_hands = "Сыграно раздач",
    .stat_hands_won = "Выиграно раздач",
    .stat_biggest_pot = "Самый большой банк",
    .stat_vs_bots = "Против ботов",
    .stat_vs_people = "Против людей",
    .stat_enters = "Входишь в раздачу",
    .stat_raises = "Повышаешь до флопа",
    .stat_pushes = "Ставок на один колл после флопа",
    .stat_showdowns_won = "Выиграно вскрытий",
    .profile_keys = "↑↓ выбор    Enter ввести ник    ←→ сменить аватар    Esc назад",
    .profile_nick_keys = "набери ник    Enter готово    Esc оставить как было",
    .nick_taken = "так зовут одного из ботов",
    .setup_bot_account = "{}: банкролл {} · раздач {} · итог {}",
    .menu_friends = "Друзья",
    .pause_friends = "Позвать друзей",
    .setup_title_online = "Свой стол",
    .setup_friends = "Друзья",
    .setup_open = "могут сесть",
    .setup_invited = "по приглашению",
    .setup_clock = "Время на ход",
    .setup_no_clock = "без таймера",
    .setup_clock_note = "когда играют друзья",
    .table_of = "стол {}",
    .table_got_up = "Тебя подняли из-за стола",
    .table_closed = "Хозяин закрыл стол",
    .host_gone = "Хозяин пропал, фишки вернулись в банкролл",
    .host_lost = "Нет связи с хозяином. Ждём ещё {}",
    .joining = "Садимся за стол к {}…",
    .joining_keys = "Esc отказаться",
    .refused_full = "За тем столом нет места",
    .refused_closed = "За тот стол только по приглашению",
    .refused_version = "У друга другая версия POHER: кому-то из вас надо обновиться",
    .refused_no_table = "У друга сейчас нет стола",
    .refused_bankroll = "Вход не подходит к тому столу",
    .friends_title = "Друзья",
    .net_connecting = "подключаюсь к сети…",
    .net_online = "в сети",
    .my_code = "Твой код",
    .my_code_note = "C копирует. Отправь другу, он вставит его в «Добавить друга».",
    .add_friend = "Добавить друга по коду",
    .code_prompt = "вставь код",
    .request_from = "Заявка от {}",
    .request_keys = "Enter принять · Backspace отклонить",
    .friends_none = "Друзей пока нет.",
    .friend_offline = "не в сети",
    .friend_online = "в сети",
    .friend_menu = "в меню",
    .friend_bots = "играет с ботами",
    .friend_hosting = "за своим столом: {} · {} из {}",
    .friend_open = "открыт",
    .friend_guest = "за столом у друга",
    .friend_version = "другая версия {}",
    .link_direct = "напрямую",
    .link_relay = "через узел",
    .friends_keys = "↑↓ выбор    Enter сесть или позвать    C скопировать код    Esc назад",
    .code_keys = "вставь или набери код    Enter добавить    Esc отмена",
    .add_sent = "Заявка отправлена: друг увидит её, когда будет в сети",
    .add_bad = "Это не код",
    .add_own = "Это твой код",
    .add_already = "Уже в друзьях",
    .add_failed = "Не получилось добавить",
    .code_copied = "Код скопирован",
    .friend_sat_down = "{} садится за стол",
    .deck_alarm = "Колода не сошлась с обещанной хозяином: карты могли подтасовать",
    .deck_doubt = "⚠ колода нечестная",
    .invite_sent = "Приглашение отправлено: {}",
    .cannot_join = "У друга сейчас нет открытого стола",
    .need_bankroll = "Для того стола нужен банкролл {}",
    .invitation_title = "{} зовёт за стол",
    .invitation_table = "{} · занято {} из {}",
    .invitation_keys = "Enter сесть (вход {})    Esc нет",
    .invitation_leave = "Ты встанешь из-за нынешнего стола",
    .history_title = "История раздач",
    .history_empty = "Раздач пока не было.",
    .history_keys = "↑↓ выбор    Enter открыть    Esc назад",
    .history_hand_keys = "↑↓ листать    ←→ соседняя раздача    Esc к списку",
    .history_hand = "раздача №{}",
};

// Names of the ranks from the two to the ace, in the forms the hand names need.
struct RankNames {
    std::string_view one;        // "ace" / "туз": a high card
    std::string_view many;       // "aces" / "тузы": two pair, full house
    std::string_view of_many;    // "aces" / "тузов": a pair of, three of, four of
    std::string_view up_to;      // "ace" / "туза": a straight or flush up to
};

constexpr RankNames kEnglishRanks[] = {
    {"two", "twos", "twos", "two"},         {"three", "threes", "threes", "three"},
    {"four", "fours", "fours", "four"},     {"five", "fives", "fives", "five"},
    {"six", "sixes", "sixes", "six"},       {"seven", "sevens", "sevens", "seven"},
    {"eight", "eights", "eights", "eight"}, {"nine", "nines", "nines", "nine"},
    {"ten", "tens", "tens", "ten"},         {"jack", "jacks", "jacks", "jack"},
    {"queen", "queens", "queens", "queen"}, {"king", "kings", "kings", "king"},
    {"ace", "aces", "aces", "ace"},
};

constexpr RankNames kRussianRanks[] = {
    {"двойка", "двойки", "двоек", "двойки"},         {"тройка", "тройки", "троек", "тройки"},
    {"четвёрка", "четвёрки", "четвёрок", "четвёрки"}, {"пятёрка", "пятёрки", "пятёрок", "пятёрки"},
    {"шестёрка", "шестёрки", "шестёрок", "шестёрки"}, {"семёрка", "семёрки", "семёрок", "семёрки"},
    {"восьмёрка", "восьмёрки", "восьмёрок", "восьмёрки"}, {"девятка", "девятки", "девяток", "девятки"},
    {"десятка", "десятки", "десяток", "десятки"},     {"валет", "валеты", "валетов", "валета"},
    {"дама", "дамы", "дам", "дамы"},                 {"король", "короли", "королей", "короля"},
    {"туз", "тузы", "тузов", "туза"},
};

const RankNames& rank_names(int rank, Lang lang) {
    const int index = rank >= 2 && rank <= game::kAce ? rank - 2 : 0;
    return lang == Lang::Ru ? kRussianRanks[index] : kEnglishRanks[index];
}

std::string rank_letter(int rank) {
    switch (rank) {
        case game::kAce: return "A";
        case game::kKing: return "K";
        case game::kQueen: return "Q";
        case game::kJack: return "J";
        default: return std::to_string(rank);
    }
}

}  // namespace

Lang system_lang() {
#ifdef _WIN32
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN ? Lang::Ru : Lang::En;
#else
    for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* value = std::getenv(name);
        if (value == nullptr || *value == '\0') continue;
        return std::string_view(value).starts_with("ru") ? Lang::Ru : Lang::En;
    }
    return Lang::En;
#endif
}

const Words& words(Lang lang) {
    return lang == Lang::Ru ? kRussian : kEnglish;
}

std::string fill(std::string_view pattern, std::initializer_list<std::string_view> arguments) {
    std::string out;
    auto next = arguments.begin();
    std::size_t from = 0;
    for (;;) {
        const std::size_t hole = pattern.find("{}", from);
        if (hole == std::string_view::npos || next == arguments.end()) break;
        out += pattern.substr(from, hole - from);
        out += *next++;
        from = hole + 2;
    }
    out += pattern.substr(from);
    return out;
}

std::string chips_text(game::Chips chips) {
    const bool negative = chips < 0;
    const std::string digits = std::to_string(negative ? -chips : chips);
    std::string out = negative ? "-" : "";
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ' ';
        out += digits[i];
    }
    return out;
}

std::string_view category_name(game::Category category, Lang lang) {
    static constexpr std::string_view english[] = {
        "High card", "Pair", "Two pair", "Three of a kind", "Straight", "Flush", "Full house", "Four of a kind",
        "Straight flush",
    };
    static constexpr std::string_view russian[] = {
        "Старшая карта", "Пара", "Две пары", "Тройка", "Стрит", "Флеш", "Фулл-хаус", "Каре", "Стрит-флеш",
    };
    const auto index = static_cast<std::size_t>(category);
    return lang == Lang::Ru ? russian[index] : english[index];
}

std::string describe_hand(game::HandValue value, Lang lang) {
    const std::array<int, 5> ranks = value.ranks();
    const RankNames& first = rank_names(ranks[0], lang);
    const RankNames& second = rank_names(ranks[1], lang);
    const bool ru = lang == Lang::Ru;
    switch (value.category()) {
        case game::Category::HighCard:
            return fill(ru ? "Старшая карта: {}" : "High card: {}", {first.one});
        case game::Category::Pair:
            return fill(ru ? "Пара {}" : "Pair of {}", {first.of_many});
        case game::Category::TwoPair:
            return fill(ru ? "Две пары: {} и {}" : "Two pair: {} and {}", {first.many, second.many});
        case game::Category::Trips:
            return fill(ru ? "Тройка {}" : "Three of a kind: {}", {first.of_many});
        case game::Category::Straight:
            return fill(ru ? "Стрит до {}" : "Straight, {} high", {first.up_to});
        case game::Category::Flush:
            return fill(ru ? "Флеш до {}" : "Flush, {} high", {first.up_to});
        case game::Category::FullHouse:
            return fill(ru ? "Фулл-хаус: {} и {}" : "Full house: {} over {}", {first.many, second.many});
        case game::Category::Quads:
            return fill(ru ? "Каре {}" : "Four of a kind: {}", {first.of_many});
        case game::Category::StraightFlush:
            if (ranks[0] == game::kAce) return ru ? "Роял-флеш" : "Royal flush";
            return fill(ru ? "Стрит-флеш до {}" : "Straight flush, {} high", {first.up_to});
    }
    return {};
}

std::string describe_cards(const std::array<game::Card, 2>& cards, const std::vector<game::Card>& board, Lang lang) {
    if (board.size() < 3) {
        if (cards[0].rank != cards[1].rank) return {};
        return fill(lang == Lang::Ru ? "Пара {}" : "Pair of {}", {rank_names(cards[0].rank, lang).of_many});
    }
    std::vector<game::Card> all(board);
    all.push_back(cards[0]);
    all.push_back(cards[1]);
    return describe_hand(game::evaluate(all), lang);
}

std::string note_text(host::Note note, game::Chips amount, Lang lang) {
    const Words& w = words(lang);
    const std::string chips = chips_text(amount);
    switch (note) {
        case host::Note::None: return {};
        case host::Note::Waiting: return std::string(w.note_waiting);
        case host::Note::Away: return std::string(w.note_away);
        case host::Note::SmallBlind: return fill(w.note_small_blind, {chips});
        case host::Note::BigBlind: return fill(w.note_big_blind, {chips});
        case host::Note::Fold: return std::string(w.note_fold);
        case host::Note::Check: return std::string(w.note_check);
        case host::Note::Call: return fill(w.note_call, {chips});
        case host::Note::Bet: return fill(w.note_bet, {chips});
        case host::Note::Raise: return fill(w.note_raise, {chips});
        case host::Note::AllIn: return fill(w.note_all_in, {chips});
        case host::Note::Show: return {};
        case host::Note::Muck: return std::string(w.note_muck);
        case host::Note::Win: return std::string(w.note_wins);
    }
    return {};
}

std::string cards_text(const std::vector<game::Card>& cards) {
    std::string out;
    for (const game::Card card : cards) {
        if (!out.empty()) out += ' ';
        out += rank_letter(card.rank);
        out += suit_sign(card.suit);
    }
    return out;
}

std::string log_text(const host::LogLine& line, Lang lang) {
    using Kind = host::LogLine::Kind;
    const Words& w = words(lang);
    const std::string chips = chips_text(line.amount);
    switch (line.kind) {
        case Kind::NewHand: return fill(w.log_hand, {std::to_string(line.number)});
        case Kind::Blind: return fill(w.log_blind, {chips});
        case Kind::Fold: return std::string(w.log_fold);
        case Kind::Check: return std::string(w.log_check);
        case Kind::Call: return fill(w.log_call, {chips});
        case Kind::Bet: return fill(w.log_bet, {chips});
        case Kind::Raise: return fill(w.log_raise, {chips});
        case Kind::AllIn: return fill(w.log_all_in, {chips});
        case Kind::Board: return cards_text(line.cards);
        case Kind::Shows: return fill(w.log_shows, {cards_text(line.cards)});
        case Kind::Mucks: return std::string(w.log_mucks);
        case Kind::Returned: return fill(w.log_returned, {chips});
        case Kind::Wins: return fill(w.log_wins, {chips});
        case Kind::Rebuy: return fill(w.log_rebuy, {chips});
        case Kind::Says: return fill(w.log_says, {bots::phrase_text(line.number, lang == Lang::Ru)});
    }
    return {};
}

}  // namespace poher::ui
