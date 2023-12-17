#include <Arduino.h>

//-----------------Ошибки-----------------
enum {
  DS3231_ERROR,        //0001 - нет связи с модулем DS3231
  DS3231_OSF_ERROR,    //0002 - сбой осцилятора модуля DS3231
  SQW_SHORT_ERROR,     //0003 - слишком короткий сигнал SQW
  SQW_LONG_ERROR,      //0004 - сигнал SQW отсутсвует или слишком длинный сигнал SQW
  TEMP_SENS_ERROR,     //0005 - выбранный датчик температуры не обнаружен
  VCC_ERROR,           //0006 - напряжения питания вне рабочего диапазона
  MEMORY_ERROR,        //0007 - сбой памяти еепром
  RESET_ERROR,         //0008 - софтовая перезагрузка
  CONVERTER_ERROR,     //0009 - сбой работы преобразователя
  PWM_OVF_ERROR,       //0010 - переполнение заполнения шим преобразователя
  STACK_OVF_ERROR,     //0011 - переполнение стека
  TICK_OVF_ERROR,      //0012 - переполнение тиков времени
  INDI_ERROR           //0013 - сбой работы динамической индикации
};
void dataUpdate(void); //процедура обработки данных
void SET_ERROR(uint8_t err); //процедура установка ошибки

//-----------------Таймеры------------------
enum {
  TMR_MS,       //таймер общего назначения
  TMR_IR,       //таймер инфракрасного приемника
  TMR_SENS,     //таймер сенсоров температуры
  TMR_PLAYER,   //таймер плеера/мелодий
  TMR_LIGHT,    //таймер сенсора яркости
  TMR_BACKL,    //таймер подсветки
  TMR_COLOR,    //таймер смены цвета подсветки
  TMR_DOT,      //таймер точек
  TMR_ANIM,     //таймер анимаций
  TIMERS_MS_NUM //количество таймеров
};
uint16_t _timer_ms[TIMERS_MS_NUM]; //таймер отсчета миллисекунд

enum {
  TMR_ALM,       //таймер тайм-аута будильника
  TMR_ALM_WAIT,  //таймер ожидания повторного включения будильника
  TMR_ALM_SOUND, //таймер отключения звука будильника
  TMR_SYNC,      //таймер синхронизации
  TMR_BURN,      //таймер антиотравления
  TMR_SHOW,      //таймер автопоказа
  TMR_GLITCH,    //таймер глюков
  TMR_SLEEP,     //таймер ухода в сон
  TIMERS_SEC_NUM //количество таймеров
};
uint16_t _timer_sec[TIMERS_SEC_NUM]; //таймер отсчета секунд

volatile uint8_t tick_ms;  //счетчик тиков миллисекунд
volatile uint8_t tick_sec; //счетчик тиков от RTC

//----------------Температура--------------
struct sensorData {
  int16_t temp; //температура со встроенного сенсора
  uint16_t press; //давление со встроенного сенсора
  uint8_t hum; //влажность со встроенного сенсора
  uint8_t type; //тип встроенного датчика температуры
  boolean init; //флаг инициализации встроенного датчика температуры
  boolean err; //ошибка встроенного датчика температуры
} sens;

//----------------Библиотеки----------------
#include <util/delay.h>

//---------------Конфигурации---------------
#include "userConfig.h"
#include "connection.h"
#include "config.h"

//----------------Периферия----------------
#include "WIRE.h"
#include "EEPROM.h"
#include "PLAYER.h"
#include "RDA.h"
#include "RTC.h"
#include "AHT.h"
#include "SHT.h"
#include "BME.h"
#include "DHT.h"
#include "DS.h"
#include "IR.h"
#include "WS.h"
#include "INDICATION.h"

//-----------------Настройки----------------
struct Settings_1 {
  uint8_t indiBright[2] = {DEFAULT_INDI_BRIGHT_N, DEFAULT_INDI_BRIGHT}; //яркость индикаторов
  uint8_t backlBright[2] = {DEFAULT_BACKL_BRIGHT_N, DEFAULT_BACKL_BRIGHT}; //яркость подсветки
  uint8_t dotBright[2] = {DEFAULT_DOT_BRIGHT_N, DEFAULT_DOT_BRIGHT}; //яркость точек
  uint8_t timeBright[2] = {DEFAULT_NIGHT_START, DEFAULT_NIGHT_END}; //время перехода яркости
  uint8_t timeHour[2] = {DEFAULT_HOUR_SOUND_START, DEFAULT_HOUR_SOUND_END}; //время звукового оповещения нового часа
  uint8_t timeSleep[2] = {DEFAULT_SLEEP_WAKE_TIME_N, DEFAULT_SLEEP_WAKE_TIME}; //время режима сна
  boolean timeFormat = DEFAULT_TIME_FORMAT; //формат времени
  boolean knockSound = DEFAULT_KNOCK_SOUND; //звук кнопок или озвучка
  uint8_t hourSound = (DEFAULT_HOUR_SOUND_TYPE & 0x03) | ((DEFAULT_HOUR_SOUND_TEMP) ? 0x80 : 0x00); //тип озвучки смены часа
  uint8_t volumeSound = CONSTRAIN((uint8_t)(PLAYER_MAX_VOL * (DEFAULT_PLAYER_VOLUME / 100.0)), PLAYER_MIN_VOL, PLAYER_MAX_VOL); //громкость озвучки
  uint8_t voiceSound = DEFAULT_VOICE_SOUND; //голос озвучки
  int8_t tempCorrect = DEFAULT_TEMP_CORRECT; //коррекция температуры
  boolean glitchMode = DEFAULT_GLITCH_MODE; //режим глюков
  uint8_t autoShowTime = DEFAULT_AUTO_SHOW_TIME; //интервал времени автопоказа
  uint8_t autoShowFlip = DEFAULT_AUTO_SHOW_ANIM; //режим анимации автопоказа
  uint8_t burnMode = DEFAULT_BURN_MODE; //режим антиотравления индикаторов
  uint8_t burnTime = BURN_PERIOD; //интервал антиотравления индикаторов
} mainSettings;

struct Settings_2 { //быстрые настройки
  uint8_t flipMode = DEFAULT_FLIP_ANIM; //режим анимации минут
  uint8_t secsMode = DEFAULT_SECONDS_ANIM; //режим анимации секунд
  uint8_t dotMode = DEFAULT_DOT_MODE; //режим точек
  uint8_t backlMode = DEFAULT_BACKL_MODE; //режим подсветки
  uint8_t backlColor = (DEFAULT_BACKL_COLOR > 25) ? (DEFAULT_BACKL_COLOR + 227) : (DEFAULT_BACKL_COLOR * 10); //цвет подсветки
} fastSettings;

struct Settings_3 { //настройки радио
  uint8_t volume = CONSTRAIN((uint8_t)(RADIO_MAX_VOL * (DEFAULT_RADIO_VOLUME / 100.0)), RADIO_MIN_VOL, RADIO_MAX_VOL); //текущая громкость
  uint8_t stationNum; //текущий номер радиостанции из памяти
  uint16_t stationsFreq = RADIO_MIN_FREQ; //текущая частота
  uint16_t stationsSave[RADIO_MAX_STATIONS] = {DEFAULT_RADIO_STATIONS}; //память радиостанций
} radioSettings;


//переменные работы с радио
struct radioData {
  boolean powerState; //текущее состояние радио
  uint8_t seekRun; //флаг автопоиска радио
  uint8_t seekAnim; //анимация автопоиска
  uint16_t seekFreq; //частота автопоиска радио
} radio;

//alarmRead/Write - час | минута | режим(0 - выкл, 1 - одиночный, 2 - вкл, 3 - по будням, 4 - по дням недели) | день недели(вс,сб,пт,чт,ср,вт,пн,null) | мелодия будильника | громкость мелодии | радиобудильник
struct alarmData {
  uint8_t num; //текущее количество будильников
  uint8_t now; //флаг активоного будильника
  uint8_t sound; //информация о текущем будильнике
  uint8_t radio; //информация о текущем будильнике
  uint8_t volume; //информация о текущем будильнике
} alarms;

//переменные обработки кнопок
struct buttonData {
  uint8_t state; //текущее состояние кнопок
  uint8_t adc; //результат опроса аналоговых кнопок
} btn;
uint8_t analogState; //флаги обновления аналоговых портов
uint16_t vcc_adc; //напряжение питания

//переменные работы с точками
struct dotData {
  boolean update; //флаг обновления точек
  boolean drive; //направление яркости
  uint8_t count; //счетчик мигания точки
#if DOTS_PORT_ENABLE
  uint8_t steps; //шаги точек
#endif
  uint8_t maxBright; //максимальная яркость точек
  uint8_t menuBright; //максимальная яркость точек в меню
  uint8_t brightStep; //шаг мигания точек
  uint16_t brightTime; //период шага мигания точек
} dot;

//переменные работы с подсветкой
struct backlightData {
  boolean drive; //направление огня
  uint8_t steps; //шаги затухания
  uint8_t color; //номер цвета
  uint8_t position; //положение огня
  uint8_t maxBright; //максимальная яркость подсветки
  uint8_t minBright; //минимальная яркость подсветки
  uint8_t menuBright; //максимальная яркость подсветки в меню
  uint8_t mode_2_step; //шаг эффекта номер 2
  uint16_t mode_2_time; //время эффекта номер 2
  uint8_t mode_4_step; //шаг эффекта номер 4
  uint8_t mode_8_step; //шаг эффекта номер 6
  uint16_t mode_8_time; //время эффекта номер 6
} backl;

//переменные работы с индикаторами
struct indiData {
  uint8_t sleepMode; //флаг режима сна индикаторов
  uint8_t maxBright; //максимальная яркость индикаторов
} indi;

//флаги анимаций
uint8_t changeBrightState; //флаг состояния смены яркости подсветки
uint8_t changeAnimState; //флаг состояния анимаций
uint8_t animShow; //флаг анимации смены времени
boolean secUpd; //флаг обновления секунды

//переменные таймера/секундомера
struct timerData {
  uint8_t mode; //режим таймера/секундомера
  uint16_t count; //счетчик таймера/секундомера
  uint16_t time = TIMER_TIME; //время таймера сек
} timer;

//переменные работы со звуками
struct soundData {
  uint8_t replay; //флаг повтора мелодии
  uint16_t semp; //текущий семпл мелодии
  uint16_t link; //ссылка на мелодию
  uint16_t size; //количество семплов мелодии
} sound;
volatile uint16_t buzz_cnt_puls; //счетчик циклов длительности
volatile uint16_t buzz_cnt_time; //счетчик циклов полуволны
uint16_t buzz_time; //циклы полуволны для работы пищалки

#define CONVERT_NUM(x) ((x[0] - 48) * 100 + (x[2] - 48) * 10 + (x[4] - 48)) //преобразовать строку в число
#define CONVERT_CHAR(x) (x - 48) //преобразовать символ в число

#define ALARM_PLAYER_VOL_MIN (uint8_t)(PLAYER_MAX_VOL * (ALARM_AUTO_VOL_MIN / 100.0))
#define ALARM_PLAYER_VOL_MAX (uint8_t)(PLAYER_MAX_VOL * (ALARM_AUTO_VOL_MAX / 100.0))
#define ALARM_PLAYER_VOL_TIME (uint16_t)(((uint16_t)ALARM_AUTO_VOL_TIME * 1000) / (ALARM_PLAYER_VOL_MAX - ALARM_PLAYER_VOL_MIN))
#define ALARM_RADIO_VOL_MIN (uint8_t)(RADIO_MAX_VOL * (ALARM_AUTO_VOL_MIN / 100.0))
#define ALARM_RADIO_VOL_MAX (uint8_t)(RADIO_MAX_VOL * (ALARM_AUTO_VOL_MAX / 100.0))
#define ALARM_RADIO_VOL_TIME (uint16_t)(((uint16_t)ALARM_AUTO_VOL_TIME * 1000) / (ALARM_RADIO_VOL_MAX - ALARM_RADIO_VOL_MIN))

#define BTN_GIST_TICK (BTN_GIST_TIME / (US_PERIOD / 1000.0)) //количество циклов для защиты от дребезга
#define BTN_HOLD_TICK (BTN_HOLD_TIME / (US_PERIOD / 1000.0)) //количество циклов после которого считается что кнопка зажата

#if BTN_TYPE
#define GET_ADC(low, high) (int16_t)(255.0 / (float)R_COEF(low, high)) //рассчет значения ацп кнопок

#define SET_MIN_ADC (uint8_t)(CONSTRAIN(GET_ADC(BTN_R_LOW, BTN_SET_R_HIGH) - BTN_ANALOG_GIST, BTN_MIN_RANGE, BTN_MAX_RANGE))
#define SET_MAX_ADC (uint8_t)(CONSTRAIN(GET_ADC(BTN_R_LOW, BTN_SET_R_HIGH) + BTN_ANALOG_GIST, BTN_MIN_RANGE, BTN_MAX_RANGE))

#define LEFT_MIN_ADC (uint8_t)(CONSTRAIN(GET_ADC(BTN_R_LOW, BTN_LEFT_R_HIGH) - BTN_ANALOG_GIST, BTN_MIN_RANGE, BTN_MAX_RANGE))
#define LEFT_MAX_ADC (uint8_t)(CONSTRAIN(GET_ADC(BTN_R_LOW, BTN_LEFT_R_HIGH) + BTN_ANALOG_GIST, BTN_MIN_RANGE, BTN_MAX_RANGE))

#define RIGHT_MIN_ADC (uint8_t)(CONSTRAIN(GET_ADC(BTN_R_LOW, BTN_RIGHT_R_HIGH) - BTN_ANALOG_GIST, BTN_MIN_RANGE, BTN_MAX_RANGE))
#define RIGHT_MAX_ADC (uint8_t)(CONSTRAIN(GET_ADC(BTN_R_LOW, BTN_RIGHT_R_HIGH) + BTN_ANALOG_GIST, BTN_MIN_RANGE, BTN_MAX_RANGE))

#define SET_CHK buttonCheckADC(SET_MIN_ADC, SET_MAX_ADC) //чтение средней аналоговой кнопки
#define LEFT_CHK buttonCheckADC(LEFT_MIN_ADC, LEFT_MAX_ADC) //чтение левой аналоговой кнопки
#define RIGHT_CHK buttonCheckADC(RIGHT_MIN_ADC, RIGHT_MAX_ADC) //чтение правой аналоговой кнопки

#if (BTN_ADD_TYPE == 2)
#define ADD_MIN_ADC (uint8_t)(CONSTRAIN(GET_ADC(BTN_R_LOW, BTN_ADD_R_HIGH) - BTN_ANALOG_GIST, BTN_MIN_RANGE, BTN_MAX_RANGE))
#define ADD_MAX_ADC (uint8_t)(CONSTRAIN(GET_ADC(BTN_R_LOW, BTN_ADD_R_HIGH) + BTN_ANALOG_GIST, BTN_MIN_RANGE, BTN_MAX_RANGE))

#define ADD_CHK buttonCheckADC(ADD_MIN_ADC, ADD_MAX_ADC) //чтение правой аналоговой кнопки
#endif
#endif

//перечисления меню настроек
enum {
  SET_TIME_FORMAT, //формат времени и анимация глюков
  SET_GLITCH_MODE, //режим глюков или громкость и голос озвучки
  SET_BTN_SOUND, //звук кнопок или озвучка смены часа и действий
  SET_HOUR_TIME, //звук смены часа
  SET_BRIGHT_TIME, //время смены яркости
  SET_INDI_BRIGHT, //яркость индикаторов
  SET_BACKL_BRIGHT, //яркость подсветки
  SET_DOT_BRIGHT, //яркость точек
  SET_TEMP_SENS, //настройка датчика температуры
  SET_AUTO_SHOW, //автопоказ данных
  SET_BURN_MODE, //анимация антиотравления индикаторов
  SET_SLEEP_TIME, //время до ухода в сон
  SET_MAX_ITEMS //максимум пунктов меню
};

//перечисления меню отладки
enum {
  DEB_AGING_CORRECT, //корректировка регистра стариния часов
  DEB_TIME_CORRECT, //корректировка хода внутреннего таймера
  DEB_DEFAULT_MIN_PWM, //минимальное значение шим
  DEB_DEFAULT_MAX_PWM, //максимальное значение шим
  DEB_HV_ADC, //значение ацп преобразователя
  DEB_IR_BUTTONS, //програмирование кнопок
  DEB_LIGHT_SENS, //калибровка датчика освещения
  DEB_RESET, //сброс настроек отладки
  DEB_MAX_ITEMS //максимум пунктов меню
};

//перечисления режимов антиотравления
enum {
  BURN_ALL, //перебор всех индикаторов
  BURN_SINGLE, //перебор одного индикатора
  BURN_SINGLE_TIME, //перебор одного индикатора с отображением времени
  BURN_EFFECT_NUM //максимум эффектов антиотравления
};
enum {
  BURN_NORMAL, //нормальный режим
  BURN_DEMO //демонстрация
};

//перечисления анимаций перебора цифр секунд
enum {
  SECS_STATIC, //без анимации
  SECS_BRIGHT, //плавное угасание и появление
  SECS_ORDER_OF_NUMBERS, //перемотка по порядку числа
  SECS_ORDER_OF_CATHODES, //перемотка по порядку катодов в лампе
  SECS_EFFECT_NUM //максимум эффектов перелистывания
};

//перечисления быстрого меню
enum {
  FAST_FLIP_MODE, //режим смены минут
  FAST_SECS_MODE, //режим смены минут
  FAST_DOT_MODE, //режим точек
  FAST_BACKL_MODE, //режим подсветки
  FAST_BACKL_COLOR //цвет подсветки
};

//перечисления режимов автопоказа
enum {
  SHOW_NULL, //показ отключен
  SHOW_DATE, //показ даты
  SHOW_YEAR, //показ года
  SHOW_DATE_YEAR, //показ даты и года
  SHOW_TEMP, //показ температуры
  SHOW_HUM, //показ влажности
  SHOW_PRESS, //показ давления
  SHOW_TEMP_HUM, //показ температуры и влажности
  SHOW_TEMP_ESP, //показ температуры из esp
  SHOW_HUM_ESP, //показ влажности из esp
  SHOW_PRESS_ESP, //показ давления из esp
  SHOW_TEMP_HUM_ESP //показ температуры и влажности из esp
};

//перечисления анимаций перебора цифр
enum {
  FLIP_BRIGHT, //плавное угасание и появление
  FLIP_ORDER_OF_NUMBERS, //перемотка по порядку числа
  FLIP_ORDER_OF_CATHODES, //перемотка по порядку катодов в лампе
  FLIP_TRAIN, //поезд
  FLIP_RUBBER_BAND, //резинка
  FLIP_GATES, //ворота
  FLIP_WAVE, //волна
  FLIP_HIGHLIGHTS, //блики
  FLIP_EVAPORATION, //испарение
  FLIP_SLOT_MACHINE, //игровой автомат
  FLIP_EFFECT_NUM //максимум эффектов перелистывания
};
enum {
  FLIP_NORMAL, //нормальный режим
  FLIP_TIME, //режим анимации времени
  FLIP_DEMO //демонстрация
};

//перечисления режимов подсветки
enum {
  BACKL_OFF, //выключена
  BACKL_STATIC, //статичная
  BACKL_PULS, //дыхание
#if BACKL_TYPE == 3
  BACKL_PULS_COLOR, //дыхание со сменой цвета при затухании
  BACKL_RUNNING_FIRE, //бегущий огонь
  BACKL_RUNNING_FIRE_COLOR, //бегущий огонь со сменой цвета
  BACKL_RUNNING_FIRE_RAINBOW, //бегущий огонь с радугой
  BACKL_RUNNING_FIRE_CONFETTI, //бегущий огонь с конфетти
  BACKL_WAVE, //волна
  BACKL_WAVE_COLOR, //волна со сменой цвета
  BACKL_WAVE_RAINBOW, //волна с радугой
  BACKL_WAVE_CONFETTI, //волна с конфетти
  BACKL_SMOOTH_COLOR_CHANGE, //плавная смена цвета
  BACKL_RAINBOW, //радуга
  BACKL_CONFETTI, //конфетти
#endif
  BACKL_EFFECT_NUM //максимум эффектов подсветки
};

//перечисления режимов точек
enum {
  DOT_OFF, //выключена
  DOT_STATIC, //статичная
#if NEON_DOT != 3
  DOT_PULS, //плавно мигает
#endif
#if NEON_DOT == 2
  DOT_TURN_BLINK_NEON, //мигание по очереди неоновых ламп
#endif
#if DOTS_PORT_ENABLE
  DOT_BLINK, //одиночное мигание
  DOT_RUNNING, //бегущая
  DOT_SNAKE, //змейка
  DOT_RUBBER_BAND, //резинка
#if (DOTS_NUM > 4) || (DOTS_TYPE == 2)
  DOT_TURN_BLINK, //мигание одной точки по очереди
#endif
#if (DOTS_NUM > 4) && (DOTS_TYPE == 2)
  DOT_DUAL_TURN_BLINK, //мигание двумя точками по очереди
#endif
#endif
  DOT_EFFECT_NUM, //максимум эффектов подсветки
  DOT_OTHER_BLINK, //дополнительное одиночное мигание
  DOT_OTHER_DOOBLE_BLINK //дополнительное двойное мигание
};

//перечисления настроек будильника
enum {
  ALARM_HOURS, //час будильника
  ALARM_MINS, //минута будильника
  ALARM_MODE, //режим будильника
  ALARM_DAYS, //день недели будильника
  ALARM_SOUND, //мелодия будильника
  ALARM_VOLUME, //громкость будильника
  ALARM_RADIO, //радиобудильник
  ALARM_STATUS, //статус будильника
  ALARM_MAX_ARR //максимальное количество данных
};

//перечисления датчиков температуры
enum {
  SENS_DS3231, //датчик DS3231
  SENS_AHT, //датчики AHT
  SENS_SHT, //датчики SHT
  SENS_BME, //датчики BME/BMP
  SENS_DS18B20, //датчики DS18B20
  SENS_DHT, //датчики DHT
  SENS_ALL //датчиков всего
};

//перечисления системных звуков
enum {
  SOUND_PASS_ERROR, //звук ошибки ввода пароля
  SOUND_RESET_SETTINGS, //звук сброса настроек
  SOUND_ALARM_DISABLE, //звук отключения будильника
  SOUND_ALARM_WAIT, //звук ожидания будильника
  SOUND_TIMER_WARN, //звук окончания таймера
  SOUND_HOUR //звук смены часа
};

//перечисления режимов воспроизведения мелодий
enum {
  REPLAY_STOP, //остановить воспроизведение
  REPLAY_ONCE, //проиграть один раз
  REPLAY_CYCLE //проиграть по кругу
};

//перечисления режимов анимации времени
enum {
  ANIM_NULL, //нет анимации
  ANIM_SECS, //запуск анимации секунд
  ANIM_MINS, //запуск анимации минут
  ANIM_MAIN, //запуск основной анимации
  ANIM_DEMO, //запуск демострации анимации
  ANIM_OTHER //запуск иной анимации
};

//перечисления режимов времени
enum {
  TIME_NIGHT, //ночной режим
  TIME_DAY //дневной режим
};

//перечисления режимов сна
enum {
  SLEEP_DISABLE, //режим сна выключен
  SLEEP_NIGHT, //ночной режим сна
  SLEEP_DAY //дневной режим сна
};

//перечисления режимов смены яркости
enum {
  CHANGE_DISABLE, //смена яркости запрещена
  CHANGE_STATIC_BACKL, //разрешено управления яркостью статичной подсветки
  CHANGE_DYNAMIC_BACKL, //разрешено управления яркостью динамичной подсветки
#if BURN_BRIGHT
  CHANGE_INDI_BLOCK, //запрещена смена яркости индикаторов
#endif
  CHANGE_ENABLE //смена яркости разрешена
};

struct Settings_4 { //расширенные настройки
  uint8_t autoShowModes[5] = {AUTO_SHOW_MODES};
  uint8_t autoShowTimes[5] = {AUTO_SHOW_TIMES};
  uint8_t alarmTime = ALARM_TIMEOUT;
  uint8_t alarmWaitTime = ALARM_WAIT;
  uint8_t alarmSoundTime = ALARM_TIMEOUT_SOUND;
  uint8_t alarmDotOn = ((ALARM_ON_BLINK_DOT > 2) ? (ALARM_ON_BLINK_DOT - 3) : (ALARM_ON_BLINK_DOT + DOT_EFFECT_NUM));
  uint8_t alarmDotWait = ((ALARM_WAIT_BLINK_DOT > 2) ? (ALARM_WAIT_BLINK_DOT - 3) : (ALARM_WAIT_BLINK_DOT + DOT_EFFECT_NUM));
} extendedSettings;

const uint8_t deviceInformation[] = { //комплектация часов
  CONVERT_CHAR(FIRMWARE_VERSION[0]),
  CONVERT_CHAR(FIRMWARE_VERSION[2]),
  CONVERT_CHAR(FIRMWARE_VERSION[4]),
  HARDWARE_VERSION,
  (DS3231_ENABLE | SENS_AHT_ENABLE | SENS_SHT_ENABLE | SENS_BME_ENABLE | SENS_PORT_ENABLE),
  SHOW_TEMP_MODE,
  LAMP_NUM,
  BACKL_TYPE,
  NEON_DOT,
  DOTS_PORT_ENABLE,
  DOTS_NUM,
  DOTS_TYPE,
  LIGHT_SENS_ENABLE,
  (BTN_ADD_TYPE | IR_PORT_ENABLE),
  TIMER_ENABLE,
  RADIO_ENABLE,
  ALARM_TYPE,
  PLAYER_TYPE,
#if PLAYER_TYPE
  PLAYER_ALARM_MAX,
#else
  SOUND_MAX(alarm_sound),
#endif
  PLAYER_VOICE_MAX,
  PLAYER_MAX_VOL
};

//переменные работы с температурой
struct mainSensorData {
  int16_t temp; //температура
  uint16_t press; //давление
  uint8_t hum; //влажность
} mainSens;

//переменные работы с шиной
struct busData {
  uint8_t position; //текущая позиция
  uint8_t counter; //счетчик байт
  uint8_t comand; //текущая команда
  uint8_t status; //статус шины
  uint8_t statusExt; //статус шины
  uint8_t buffer[10]; //буфер шины
} bus;

#define BUS_WAIT_DATA 0x00

#define BUS_WRITE_TIME 0x01
#define BUS_READ_TIME 0x02

#define BUS_WRITE_FAST_SET 0x03
#define BUS_READ_FAST_SET 0x04

#define BUS_WRITE_MAIN_SET 0x05
#define BUS_READ_MAIN_SET 0x06

#define BUS_READ_ALARM_NUM 0x07
#define BUS_WRITE_SELECT_ALARM 0x08
#define BUS_READ_SELECT_ALARM 0x09
#define BUS_WRITE_ALARM_DATA 0x0A
#define BUS_READ_ALARM_DATA 0x0B
#define BUS_DEL_ALARM 0x0C
#define BUS_NEW_ALARM 0x0D

#define BUS_READ_RADIO_SET 0x0E
#define BUS_WRITE_RADIO_STA 0x0F
#define BUS_WRITE_RADIO_VOL 0x10
#define BUS_WRITE_RADIO_FREQ 0x11
#define BUS_WRITE_RADIO_MODE 0x12
#define BUS_WRITE_RADIO_POWER 0x13
#define BUS_SEEK_RADIO_UP 0x14
#define BUS_SEEK_RADIO_DOWN 0x15
#define BUS_READ_RADIO_POWER 0x16

#define BUS_CHECK_TEMP 0x17
#define BUS_READ_TEMP 0x18

#define BUS_WRITE_EXTENDED_SET 0x19
#define BUS_READ_EXTENDED_SET 0x1A

#define BUS_SET_SHOW_TIME 0x1B
#define BUS_SET_BURN_TIME 0x1C
#define BUS_SET_UPDATE 0x1E

#define BUS_WRITE_TIMER_SET 0x1F
#define BUS_READ_TIMER_SET 0x20
#define BUS_WRITE_TIMER_MODE 0x21

#define BUS_WRITE_SENS_DATA 0x22

#define BUS_CONTROL_DEVICE 0xFA

#define BUS_TEST_FLIP 0xFB
#define BUS_TEST_SOUND 0xFC

#define BUS_SELECT_BYTE 0xFD
#define BUS_READ_STATUS 0xFE
#define BUS_READ_DEVICE 0xFF

#define DEVICE_RESET 0xCC
#define DEVICE_REBOOT 0xEE

enum {
  BUS_COMMAND_BIT_0,
  BUS_COMMAND_BIT_1,
  BUS_COMMAND_BIT_2,
  BUS_COMMAND_BIT_3,
  BUS_COMMAND_BIT_4,
  BUS_COMMAND_BIT_5,
  BUS_COMMAND_WAIT,
  BUS_COMMAND_UPDATE
};
enum {
  BUS_COMMAND_NULL,
  BUS_COMMAND_RADIO_MODE,
  BUS_COMMAND_RADIO_POWER,
  BUS_COMMAND_RADIO_SEEK_UP,
  BUS_COMMAND_RADIO_SEEK_DOWN,
  BUS_COMMAND_TIMER_MODE
};
enum {
  BUS_EXT_COMMAND_CHECK_TEMP,
  BUS_EXT_COMMAND_SEND_TIME,
#if RADIO_ENABLE
  BUS_EXT_COMMAND_RADIO_VOL,
  BUS_EXT_COMMAND_RADIO_FREQ,
#endif
  BUS_EXT_MAX_DATA
};

enum {
  STATUS_UPDATE_TIME_SET,
  STATUS_UPDATE_MAIN_SET,
  STATUS_UPDATE_FAST_SET,
  STATUS_UPDATE_RADIO_SET,
  STATUS_UPDATE_EXTENDED_SET,
  STATUS_UPDATE_ALARM_SET,
  STATUS_UPDATE_SENS_DATA,
  STATUS_MAX_DATA
};
uint8_t deviceStatus; //состояние часов

enum {
  MEM_UPDATE_TIME_SET,
  MEM_UPDATE_MAIN_SET,
  MEM_UPDATE_FAST_SET,
  MEM_UPDATE_RADIO_SET,
  MEM_UPDATE_EXTENDED_SET,
  MEM_MAX_DATA
};
uint8_t memoryCheck;

//перечисления основных программ
enum {
  INIT_PROGRAM,      //инициализация
  MAIN_PROGRAM,      //главный экран
  TEMP_PROGRAM,      //температура
  DATE_PROGRAM,      //текущая дата
  WARN_PROGRAM,      //предупреждение таймера
  ALARM_PROGRAM,     //тревога будильника
  RADIO_PROGRAM,     //радиоприемник
  TIMER_PROGRAM,     //таймер-секундомер
  SLEEP_PROGRAM,     //режим сна индикаторов
  FAST_SET_PROGRAM,  //быстрые настройки
  MAIN_SET_PROGRAM,  //основные настройки
  CLOCK_SET_PROGRAM, //настройки времени
  ALARM_SET_PROGRAM  //настройки будильника
};
uint8_t mainTask = INIT_PROGRAM; //переключатель подпрограмм

#define EEPROM_BLOCK_TIME EEPROM_BLOCK_NULL //блок памяти времени
#define EEPROM_BLOCK_SETTINGS_FAST (EEPROM_BLOCK_TIME + sizeof(RTC)) //блок памяти настроек свечения
#define EEPROM_BLOCK_SETTINGS_MAIN (EEPROM_BLOCK_SETTINGS_FAST + sizeof(fastSettings)) //блок памяти основных настроек
#define EEPROM_BLOCK_SETTINGS_RADIO (EEPROM_BLOCK_SETTINGS_MAIN + sizeof(mainSettings)) //блок памяти настроек радио
#define EEPROM_BLOCK_SETTINGS_DEBUG (EEPROM_BLOCK_SETTINGS_RADIO + sizeof(radioSettings)) //блок памяти настроек отладки
#define EEPROM_BLOCK_SETTINGS_EXTENDED (EEPROM_BLOCK_SETTINGS_DEBUG + sizeof(debugSettings)) //блок памяти расширенных настроек
#define EEPROM_BLOCK_ERROR (EEPROM_BLOCK_SETTINGS_EXTENDED + sizeof(extendedSettings)) //блок памяти ошибок
#define EEPROM_BLOCK_EXT_ERROR (EEPROM_BLOCK_ERROR + 1) //блок памяти расширеных ошибок
#define EEPROM_BLOCK_ALARM (EEPROM_BLOCK_EXT_ERROR + 1) //блок памяти количества будильников

#define EEPROM_BLOCK_CRC_DEFAULT (EEPROM_BLOCK_ALARM + 1) //блок памяти контрольной суммы настроек
#define EEPROM_BLOCK_CRC_TIME (EEPROM_BLOCK_CRC_DEFAULT + 1) //блок памяти контрольной суммы времени
#define EEPROM_BLOCK_CRC_FAST (EEPROM_BLOCK_CRC_TIME + 1) //блок памяти контрольной суммы быстрых настроек
#define EEPROM_BLOCK_CRC_MAIN (EEPROM_BLOCK_CRC_FAST + 1) //блок памяти контрольной суммы основных настроек
#define EEPROM_BLOCK_CRC_RADIO (EEPROM_BLOCK_CRC_MAIN + 1) //блок памяти контрольной суммы настроек радио
#define EEPROM_BLOCK_CRC_DEBUG (EEPROM_BLOCK_CRC_RADIO + 1) //блок памяти контрольной суммы настроек отладки
#define EEPROM_BLOCK_CRC_DEBUG_DEFAULT (EEPROM_BLOCK_CRC_DEBUG + 1) //блок памяти контрольной суммы настроек отладки
#define EEPROM_BLOCK_CRC_EXTENDED (EEPROM_BLOCK_CRC_DEBUG_DEFAULT + 1) //блок памяти контрольной суммы расширеных настроек
#define EEPROM_BLOCK_CRC_ERROR (EEPROM_BLOCK_CRC_EXTENDED + 1) //блок контрольной суммы памяти ошибок
#define EEPROM_BLOCK_CRC_EXT_ERROR (EEPROM_BLOCK_CRC_ERROR + 1) //блок контрольной суммы памяти расширеных ошибок
#define EEPROM_BLOCK_CRC_ALARM (EEPROM_BLOCK_CRC_EXT_ERROR + 1) //блок контрольной суммы количества будильников
#define EEPROM_BLOCK_ALARM_DATA (EEPROM_BLOCK_CRC_ALARM + 1) //первая ячейка памяти будильников

#define MAX_ALARMS ((EEPROM_BLOCK_MAX - EEPROM_BLOCK_ALARM_DATA) / ALARM_MAX_ARR) //максимальное количество будильников

//--------------------------------------Главный цикл программ---------------------------------------------------
int main(void); //главный цикл программ

//--------------------------------------Инициализация---------------------------------------------------
void INIT_SYSTEM(void); //инициализация

// ==========================
//-----------------------------Прерывание от RTC--------------------------------
// #if SQW_PORT_ENABLE
// ISR(INT0_vect) //внешнее прерывание на пине INT0 - считаем секунды с RTC
// {
//   tick_sec++; //прибавляем секунду
// }
// #endif
//-----------------------Прерывание сигнала для пищалки-------------------------
// #if !PLAYER_TYPE
// ISR(TIMER2_COMPB_vect) //прерывание сигнала для пищалки
// {
//   if (!buzz_cnt_time) { //если циклы полуволны кончились
//     BUZZ_INV; //инвертируем бузер
//     buzz_cnt_time = buzz_time; //устанавливаем циклы полуволны
//     if (!--buzz_cnt_puls) { //считаем циклы времени работы бузера
//       BUZZ_OFF; //если циклы кончились, выключаем бузер
//       TIMSK2 &= ~(0x01 << OCIE2B); //выключаем таймер
//     }
//   }
//   if (buzz_cnt_time > 255) buzz_cnt_time -= 255; //считаем циклы полуволны
//   else if (buzz_cnt_time) { //если остался хвост
//     OCR2B += buzz_cnt_time; //устанавливаем хвост
//     buzz_cnt_time = 0; //сбрасываем счетчик циклов полуволны
//   }
// }
// #endif
//======================================

//-----------------------------Установка ошибки---------------------------------
void SET_ERROR(uint8_t err); //установка ошибки

//--------------------------Установка таймеров анимаций-------------------------
void setAnimTimers(void); //установка таймеров анимаций

//-------------------------Разрешить анимации подсветки-------------------------
void backlAnimEnable(void); //разрешить анимации подсветки

//-------------------------Запретить анимации подсветки-------------------------
void backlAnimDisable(void); //запретить анимации подсветки

//----------------------Разрешить анимацию секундных точек----------------------
void dotAnimEnable(void); //разрешить анимацию секундных точек

//----------------------Запретить анимацию секундных точеки---------------------
void dotAnimDisable(void); //запретить анимацию секундных точек

//----------------------------Разрешить смену яркости---------------------------
void changeBrightEnable(void); //разрешить смену яркости

//----------------------------Запретить смену яркости---------------------------
void changeBrightDisable(uint8_t _state); //запретить смену яркости

//-----------------------------Расчет шага яркости-----------------------------
uint8_t setBrightStep(uint16_t _brt, uint16_t _step, uint16_t _time); //расчет шага яркости

//-------------------------Расчет периода шага яркости--------------------------
uint16_t setBrightTime(uint16_t _brt, uint16_t _step, uint16_t _time); //расчет периода шага яркости

//---------------------Установка яркости от времени суток-----------------------------
boolean checkHourStrart(uint8_t _start, uint8_t _end); //установка яркости от времени суток

//-------------------------Получить 12-ти часовой формат------------------------
uint8_t get_12h(uint8_t timeH); //получить 12-ти часовой формат

//---------------------------------Получить время со сдвигом фазы-----------------------------------------
uint16_t getPhaseTime(uint8_t time, int8_t phase); //получить время со сдвигом фазы

//------------------------Сверка контрольной суммы---------------------------------
void checkCRC(uint8_t* crc, uint8_t data); //сверка контрольной суммы

//------------------------Проверка байта в памяти-------------------------------
boolean checkByte(uint8_t cell, uint8_t cellCRC); //проверка байта в памяти

//-----------------------Обновление байта в памяти-------------------------------
void updateByte(uint8_t data, uint8_t cell, uint8_t cellCRC); //обновление байта в памяти

//------------------------Проверка данных в памяти-------------------------------
boolean checkData(uint8_t size, uint8_t cell, uint8_t cellCRC); //проверка данных в памяти

//-----------------------Обновление данных в памяти-------------------------------
void updateData(uint8_t* str, uint8_t size, uint8_t cell, uint8_t cellCRC); //обновление данных в памяти

//--------------------Проверка контрольной суммы настроек--------------------------
boolean checkSettingsCRC(void); //проверка контрольной суммы настроек

//------------------Проверка контрольной суммы настроек отладки---------------------
boolean checkDebugSettingsCRC(void); //проверка контрольной суммы настроек отладки

//-------------------Установить флаг обновления данных в памяти---------------------
void setUpdateMemory(uint8_t mask); //установить флаг обновления данных в памяти

//----------------------------Обновить данные в памяти------------------------------
void updateMemory(void); //обновить данные в памяти

//-----------------Проверка установленного датчика температуры----------------------
void checkTempSens(void); //проверка установленного датчика температуры

//-------------------------Обновить показания температуры---------------------------
void updateTemp(void); //обновить показания температуры

//------------------------Получить показания температуры---------------------------
int16_t getTemperatureData(void);

//--------------------------Получить знак температуры------------------------------
boolean getTemperatureSign(void);

//------------------------Получить показания температуры---------------------------
uint16_t getTemperature(void);

//--------------------------Получить показания давления----------------------------
uint16_t getPressure(void);

//-------------------------Получить показания влажности----------------------------
uint8_t getHumidity(void);

//------------------------Получить показания температуры---------------------------
int16_t getTemperatureData(uint8_t data);

//--------------------------Получить знак температуры------------------------------
boolean getTemperatureSign(uint8_t data);

//------------------------Получить показания температуры---------------------------
uint16_t getTemperature(uint8_t data);

//--------------------------Получить показания давления----------------------------
uint16_t getPressure(uint8_t data);

//-------------------------Получить показания влажности----------------------------
uint8_t getHumidity(uint8_t data);

//-----------------Обновление предела удержания напряжения-------------------------
void updateTresholdADC(void); //обновление предела удержания напряжения

//------------------------Обработка аналоговых входов------------------------------
void analogUpdate(void); //обработка аналоговых входов

//----------------------Чтение напряжения питания----------------------------------
void checkVCC(void); //чтение напряжения питания

//----------------Обновление зон  сенсора яркости освещения------------------------
void lightSensZoneUpdate(uint8_t min, uint8_t max); //обновление зон сенсора яркости освещения

//-------------------Обработка сенсора яркости освещения---------------------------
void lightSensUpdate(void); //обработка сенсора яркости освещения

//-------------------Проверка сенсора яркости освещения----------------------------
void lightSensCheck(void); //проверка сенсора яркости освещения

//-----------------------Проверка аналоговой кнопки--------------------------------
inline boolean buttonCheckADC(uint8_t minADC, uint8_t maxADC); //проверка аналоговой кнопки

//---------------------------Проверка кнопок---------------------------------------
inline uint8_t buttonState(void); //проверка кнопок

//--------------------------Обновление кнопок--------------------------------------
inline uint8_t buttonStateUpdate(void); //обновление кнопок

//------------------Проверка модуля часов реального времени-------------------------
void checkRTC(void); //проверка модуля часов реального времени

//-----------------------------Проверка ошибок-------------------------------------
void checkErrors(void); //проверка ошибок

//---------------------------Проверка системы---------------------------------------
void test_system(void); //проверка системы

//-----------------------------Проверка пароля------------------------------------
boolean check_pass(void); //проверка пароля

//-----------------------------Отладка------------------------------------
void debug_menu(void); //отладка

//--------------------------------Генерация частот бузера----------------------------------------------
void buzz_pulse(uint16_t freq, uint16_t time); //генерация частоты бузера (частота 10..10000, длительность мс.)

//--------------------------------Воспроизведение мелодии-----------------------------------------------
void melodyUpdate(void); //воспроизведение мелодии

//----------------------------Запуск воспроизведения мелодии---------------------------------------------
void melodyPlay(uint8_t melody, uint16_t link, uint8_t replay); //запуск воспроизведения мелодии

//---------------------------Остановка воспроизведения мелодии-------------------------------------------
void melodyStop(void); //остановка воспроизведения мелодии

//---------------------------------Инициализация будильника----------------------------------------------
void alarmInit(void); //инициализация будильника

//-----------------------------------Отключение будильника------------------------------------------------
void alarmDisable(void); //отключение будильника

//--------------------------------------Сброс будильника--------------------------------------------------
void alarmReset(void); //сброс будильника

//-----------------------------Получить основные данные будильника-----------------------------------------
uint8_t alarmRead(uint8_t almNum, uint8_t almDataPos); //получить основные данные будильника

//-----------------------------Записать основные данные будильника-----------------------------------------
void alarmWrite(uint8_t almNum, uint8_t almDataPos, uint8_t almData); //записать основные данные будильника

//--------------------------Получить блок основных данных будильника---------------------------------------
void alarmReadBlock(uint8_t almNum, uint8_t* data); //получить блок основных данных будильника

//---------------------------Записать блок основных данных будильника--------------------------------------
void alarmWriteBlock(uint8_t almNum, uint8_t* data); //записать блок основных данных будильника

//---------------------------------Создать новый будильник-------------------------------------------------
void newAlarm(void); //создать новый будильник

//-----------------------------------Удалить будильник-----------------------------------------------------
void delAlarm(uint8_t alarm); //удалить будильник

//----------------------------------Проверка будильников----------------------------------------------------
void checkAlarms(uint8_t check); //проверка будильников

//-------------------------------Обновление данных будильников---------------------------------------------
void alarmDataUpdate(void); //обновление данных будильников

//----------------------------------Тревога будильника---------------------------------------------------------
uint8_t alarmWarn(void); //тревога будильника

#if ESP_ENABLE
//-------------------------------------Проверка статуса шины-------------------------------------------
uint8_t busCheck(void); //проверка статуса шины

//-------------------------------------Проверка команды шины-------------------------------------------
void busCommand(void); //проверка команды шины

#endif
//------------------------------------Обновление статуса шины------------------------------------------
uint8_t busUpdate(void); //обновление статуса шины

//----------------------------------Обработка данных------------------------------------------------
void dataUpdate(void); //обработка данных

//----------------------------Настройки времени----------------------------------
uint8_t settings_time(void); //настройки времени

//-----------------------------Настройка будильника------------------------------------
uint8_t settings_singleAlarm(void); //настройка будильника

//-----------------------------Настройка будильников------------------------------------
uint8_t settings_multiAlarm(void); //настройка будильников

//-----------------------------Настроки основные------------------------------------
uint8_t settings_main(void); //настроки основные

//----------------------------Воспроизвести температуру--------------------------------------
void speakTemp(boolean mode); //воспроизвести температуру

//------------------------------Воспроизвести влажность---------------------------------------
void speakHum(void); //воспроизвести влажность

//-------------------------------Воспроизвести давление---------------------------------------
void speakPress(void); //воспроизвести давление

//-----------------------------Установить разделяющую точку-----------------------------------
void setDivDot(boolean set);
//--------------------------------Показать температуру----------------------------------------
uint8_t showTemp(void); //показать температуру

//-------------------------------Воспроизвести время--------------------------------
void speakTime(boolean mode); //воспроизвести время

//----------------------------------Показать дату-----------------------------------
uint8_t showDate(void); //показать дату

//--------------------------------Меню автоматического показа----------------------------------------
void autoShowMenu(void); //меню автоматического показа

//-------------------------------Получить значение быстрых настроек---------------------------------
uint8_t getFastSetData(uint8_t pos); //получить значение быстрых настроек

//----------------------------------Переключение быстрых настроек-----------------------------------
uint8_t fastSetSwitch(void); //переключение быстрых настроек

//-------------------------Включить питание радиоприемника------------------------------
void radioPowerOn(void); //включить питание радиоприемника

//------------------------Выключить питание радиоприемника------------------------------
void radioPowerOff(void); //выключить питание радиоприемника

//--------------------------Вернуть питание радиоприемника------------------------------
void radioPowerRet(void); //вернуть питание радиоприемника

//------------------------Переключить питание радиоприемника-----------------------------
void radioPowerSwitch(void); //переключить питание радиоприемника

//--------------------------Поиск радиостанции в памяти---------------------------------
void radioSearchStation(void); //поиск радиостанции в памяти

//-----------------------Переключить радиостанцию в памяти------------------------------
void radioSwitchStation(boolean _sta); //переключить радиостанцию в памяти

//------------------------Остановка автопоиска радиостанции-----------------------------
void radioSeekStop(void); //остановка автопоиска радиостанции

//------------------------Автопоиск радиостанций вверх-----------------------------
void radioSeekUp(void); //автопоиск радиостанций

//------------------------Автопоиск радиостанций вниз-----------------------------
void radioSeekDown(void); //автопоиск радиостанций

//-----------------------------Быстрые настройки радио-----------------------------------
uint8_t radioFastSettings(void); //быстрые настройки радио

//------------------------------Меню настроек радио-------------------------------------
boolean radioMenuSettings(void); //меню настроек радио

//---------------------------------Радиоприемник----------------------------------------
uint8_t radioMenu(void); //радиоприемник

//--------------------------------Тревога таймера----------------------------------------
uint8_t timerWarn(void); //тревога таймера

//----------------------------Настройки таймера----------------------------------
void timerSettings(void); //настройки таймера

//--------------------------------Таймер-секундомер----------------------------------------
uint8_t timerStopwatch(void); //таймер-секундомер

//------------------------------------Звук смены часа------------------------------------
void hourSound(void); //звук смены часа

//---------------------Установка яркости от времени суток-----------------------------
void changeBright(void); //установка яркости от времени суток

#if BACKL_TYPE == 3
//----------------------------------Анимация подсветки---------------------------------
void backlEffect(void); //анимация подсветки

#elif BACKL_TYPE
//----------------------------------Мигание подсветки---------------------------------
void backlFlash(void); //мигание подсветки
#endif
//--------------------------------Мигание точек------------------------------------
void dotFlash(void); //мигание точек

//---------------------------Получить анимацию точек-------------------------------
uint8_t dotGetMode(void); //получить анимацию точек

//-----------------------------Сброс анимации точек--------------------------------
void dotReset(uint8_t state); //сброс анимации точек

//---------------------------------Режим сна индикаторов---------------------------------
uint8_t sleepIndi(void); //режим сна индикаторов

//------------------------------------Имитация глюков------------------------------------
void glitchIndi(void); //имитация глюков

//----------------------------Антиотравление индикаторов-------------------------------
void burnIndi(uint8_t mode, boolean demo); //антиотравление индикаторов

//-------------------------------Анимация секунд-----------------------------------
#if LAMP_NUM > 4
void flipSecs(void); //анимация секунд
#endif
//----------------------Обновить буфер анимации текущего времени--------------------
void animUpdateTime(void); //обновить буфер анимации текущего времени

//----------------------------------Анимация цифр-----------------------------------
void animIndi(uint8_t mode, uint8_t type); //анимация цифр

//----------------------------------Анимация цифр-----------------------------------
void flipIndi(uint8_t mode, uint8_t type); //анимация цифр

//-----------------------------Главный экран------------------------------------------------
uint8_t mainScreen(void); //главный экран