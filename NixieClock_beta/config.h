#define BOARD_TYPE 4  // тип платы часов(0 - IN-12 (индикаторы стоят правильно) | 1 - IN-12 turned (индикаторы перевёрнуты) | 2 - IN-14 (обычная и neon dot) | 3 другие индикаторы)

#define DOT_BRIGHT 20       //яркость точки дневная(1..255)
#define DOT_TIMER 4         //шаг яркости точки(мс)
#define DOT_TIME 200        //время мигания точки(мс)

#define MIN_PWM 100 //минимальная скважность(индикаторы выключены, яркость минимальная)
#define MAX_PWM 180 //максимальная скважность(индикаторы включены, яркость максимальная)

#define BTN_GIST_TICK    8    //количество циклов для защиты от дребезга(0..255)(1 цикл -> 4мс)
#define BTN_HOLD_TICK    127   //количество циклов после которого считается что кнопка зажата(0..255)(1 цикл -> 4мс)
