/*
  Arduino IDE 1.8.13 версия прошивки 1.4.7 релиз от 06.02.22
  Специльно для проекта "Часы на ГРИ и Arduino v2 | AlexGyver"
  Страница проекта - https://alexgyver.ru/nixieclock_v2

  Исходник - https://github.com/radon-lab/NixieClock
  Автор Radon-lab.
*/
//-----------------Таймеры------------------
enum {
  TMR_SENS, //таймер сенсоров температуры
  TMR_MS, //таймер общего назначения
  TMR_MELODY, //таймер мелодий
  TMR_BACKL, //таймер подсветки
  TMR_DOT, //таймер точек
  TMR_ANIM, //таймер анимаций
  TMR_GLITCH, //таймер глюков
  TIMERS_NUM //количество таймеров
};
uint16_t _timer_ms[TIMERS_NUM]; //таймер отсчета миллисекунд

//----------------Библиотеки----------------
#include <util/delay.h>

//---------------Конфигурации---------------
#include "config.h"
#include "connection.h"
#include "indiDisp.h"
#include "wire.h"
#include "EEPROM.h"
#include "BME.h"
#include "RTC.h"
#include "DHT.h"
#include "DS.h"
#include "WS.h"

//----------------Настройки----------------
struct Settings_1 {
  uint8_t indiBright[2] = {DEFAULT_INDI_BRIGHT_N, DEFAULT_INDI_BRIGHT}; //яркость индикаторов
  uint8_t timeBright[2] = {DEFAULT_NIGHT_START, DEFAULT_NIGHT_END}; //время перехода яркости
  uint8_t timeHour[2] = {DEFAULT_HOUR_SOUND_START, DEFAULT_HOUR_SOUND_END}; //время звукового оповещения нового часа
  boolean timeFormat = DEFAULT_TIME_FORMAT; //формат времени
  boolean knock_sound = DEFAULT_KNOCK_SOUND; //звук кнопок
  uint8_t sensorSet = DEFAULT_TEMP_SENSOR; //сенсор температуры
  int8_t tempCorrect = DEFAULT_TEMP_CORRECT; //коррекция температуры
  boolean glitchMode = DEFAULT_GLITCH_MODE; //режим глюков
  uint16_t timePeriod = US_PERIOD; //коррекция хода внутреннего осцилятора
  uint8_t autoTempTime = DEFAULT_AUTO_TEMP_TIME; //интервал времени показа температуры
} mainSettings;

struct Settings_2 {
  uint8_t flipMode = DEFAULT_FLIP_ANIM; //режим анимации
  uint8_t backlMode = DEFAULT_BACKL_MODE; //режим подсветки
  uint8_t dotMode = DEFAULT_DOT_MODE; //режим точек
} fastSettings;

//переменные обработки кнопок
uint16_t btn_tmr; //таймер тиков обработки кнопок
boolean btn_check; //флаг разрешения опроса кнопки
boolean btn_state; //флаг текущего состояния кнопки

enum {
  KEY_NULL,        //кнопка не нажата
  LEFT_KEY_PRESS,  //клик левой кнопкой
  LEFT_KEY_HOLD,   //удержание левой кнопки
  RIGHT_KEY_PRESS, //клик правой кнопкой
  RIGHT_KEY_HOLD,  //удержание правой кнопки
  SET_KEY_PRESS,   //клик средней кнопкой
  SET_KEY_HOLD,    //удержание средней кнопки
  ADD_KEY_PRESS,   //клик дополнительной кнопкой
  ADD_KEY_HOLD     //удержание дополнительной кнопки
};

boolean _animShow; //флаг анимации
boolean _sec; //флаг обновления секунды
boolean _dot; //флаг обновления точек

uint16_t FLIP_2_SPEED = FLIP_MODE_2_TIME; //скорость эффекта 2

uint8_t dotBrightStep; //шаг мигания точек
uint8_t dotBrightTime; //период шага мигания точек
uint8_t dotMaxBright; //максимальная яркость точек
uint8_t backlBrightTime; //период шага "дыхания" подсветки
uint8_t backlMaxBright; //максимальная яркость подсветки
uint8_t indiMaxBright; //максимальная яркость индикаторов

//alarmRead/Write - час | минута | режим(0 - выкл, 1 - одиночный, 2 - вкл, 3 - по будням, 4 - по дням недели) | день недели(вс,сб,пт,чт,ср,вт,пн,null) | мелодия будильника
uint8_t alarms_num; //текущее количество будильников

boolean alarmEnabled; //флаг включенного будильника
boolean alarmWaint; //флаг ожидания звука будильника
uint8_t alarm; //флаг активации будильника
uint8_t minsAlarm; //таймер полного отключения будильника
uint8_t minsAlarmSound; //таймер ожидания отключения звука будильника
uint8_t minsAlarmWaint; //таймер ожидания повторного включения будильника

uint8_t _tmrBurn; //таймер активации антиотравления
uint8_t _tmrTemp; //таймер автоматического отображения температуры
uint8_t _tmrGlitch; //таймер активации глюков

uint8_t timerMode; //режим таймера/секундомера
uint16_t timerCnt; //счетчик таймера/секундомера
uint16_t timerTime = TIMER_TIME; //время таймера сек

volatile uint16_t cnt_puls; //количество циклов для работы пищалки
volatile uint16_t cnt_freq; //частота для генерации звука пищалкой
uint16_t tmr_score; //частота для генерации звука пищалкой

uint8_t semp; //переключатель семплов мелодии
#define MELODY_PLAY(melody) _melody_chart(melody) //воспроизведение мелодии
#define MELODY_RESET semp = 0; _timer_ms[TMR_MELODY] = 0 //сброс мелодии

#define EEPROM_BLOCK_TIME EEPROM_BLOCK_NULL //блок памяти времени
#define EEPROM_BLOCK_SETTINGS_FAST (EEPROM_BLOCK_TIME + sizeof(timeRTC)) //блок памяти настроек свечения
#define EEPROM_BLOCK_SETTINGS_MAIN (EEPROM_BLOCK_SETTINGS_FAST + sizeof(fastSettings)) //блок памяти основных настроек
#define EEPROM_BLOCK_ALARM (EEPROM_BLOCK_SETTINGS_MAIN + sizeof(mainSettings)) //блок памяти количества будильников

#define EEPROM_BLOCK_CRC (EEPROM_BLOCK_ALARM + sizeof(alarms_num)) //блок памяти контрольной суммы настроек
#define EEPROM_BLOCK_CRC_TIME (EEPROM_BLOCK_CRC + 1) //блок памяти контрольной суммы времени
#define EEPROM_BLOCK_CRC_MAIN (EEPROM_BLOCK_CRC_TIME + 1) //блок памяти контрольной суммы основных настроек
#define EEPROM_BLOCK_CRC_FAST (EEPROM_BLOCK_CRC_MAIN + 1) //блок памяти контрольной суммы быстрых настроек
#define EEPROM_BLOCK_ALARM_DATA (EEPROM_BLOCK_CRC_FAST + 1) //первая ячейка памяти будильников

#define MAX_ALARMS ((1023 - EEPROM_BLOCK_ALARM_DATA) / 5) //максимальное количество будильников

#if BTN_TYPE
#define SET_CHK checkKeyADC(BTN_SET_MIN, BTN_SET_MAX) //чтение средней аналоговой кнопки
#define LEFT_CHK checkKeyADC(BTN_LEFT_MIN, BTN_LEFT_MAX) //чтение левой аналоговой кнопки
#define RIGHT_CHK checkKeyADC(BTN_RIGHT_MIN, BTN_RIGHT_MAX) //чтение правой аналоговой кнопки
#endif

//----------------------------------Инициализация----------------------------------
int main(void) //инициализация
{
#if BTN_TYPE
  initADC(); //инициализация АЦП
#else
  SET_INIT; //инициализация средней кнопки
  LEFT_INIT; //инициализация левой кнопки
  RIGHT_INIT; //инициализация правой кнопки
#endif

  ADD_INIT; //инициализация дополнительной кнопки
  CONV_INIT; //инициализация преобразователя
  SQW_INIT; //инициализация счета секунд
  BACKL_INIT; //инициализация подсветки
  BUZZ_INIT; //инициализация бузера

  uartDisable(); //отключение uart

  if (checkSettingsCRC() || !SET_CHK) { //если контрольная сумма не совпала или зажата средняя кнопка, восстанавливаем из переменных
    updateData((uint8_t*)&timeRTC, sizeof(timeRTC), EEPROM_BLOCK_TIME, EEPROM_BLOCK_CRC_TIME); //записываем дату и время в память
    updateData((uint8_t*)&fastSettings, sizeof(fastSettings), EEPROM_BLOCK_SETTINGS_FAST, EEPROM_BLOCK_CRC_FAST); //записываем настройки яркости в память
    updateData((uint8_t*)&mainSettings, sizeof(mainSettings), EEPROM_BLOCK_SETTINGS_MAIN, EEPROM_BLOCK_CRC_MAIN); //записываем основные настройки в память
    EEPROM_UpdateByte(EEPROM_BLOCK_ALARM, alarms_num); //записываем количество будильников в память
  }
  else if (LEFT_CHK) { //иначе загружаем настройки из памяти
    if (checkData(sizeof(timeRTC), EEPROM_BLOCK_TIME, EEPROM_BLOCK_CRC_TIME)) updateData((uint8_t*)&timeRTC, sizeof(timeRTC), EEPROM_BLOCK_TIME, EEPROM_BLOCK_CRC_TIME); //записываем дату и время в память
    if (checkData(sizeof(fastSettings), EEPROM_BLOCK_SETTINGS_FAST, EEPROM_BLOCK_CRC_FAST)) updateData((uint8_t*)&fastSettings, sizeof(fastSettings), EEPROM_BLOCK_SETTINGS_FAST, EEPROM_BLOCK_CRC_FAST); //записываем настройки яркости в память
    else EEPROM_ReadBlock((uint16_t)&fastSettings, EEPROM_BLOCK_SETTINGS_FAST, sizeof(fastSettings)); //считываем настройки яркости из памяти
    if (checkData(sizeof(mainSettings), EEPROM_BLOCK_SETTINGS_MAIN, EEPROM_BLOCK_CRC_MAIN)) updateData((uint8_t*)&mainSettings, sizeof(mainSettings), EEPROM_BLOCK_SETTINGS_MAIN, EEPROM_BLOCK_CRC_MAIN); //записываем основные настройки в память
    else EEPROM_ReadBlock((uint16_t)&mainSettings, EEPROM_BLOCK_SETTINGS_MAIN, sizeof(mainSettings)); //считываем основные настройки из памяти
    alarms_num = EEPROM_ReadByte(EEPROM_BLOCK_ALARM); //считываем количество будильников из памяти
  }

#if USE_ONE_ALARM
  initAlarm(); //инициализация будильника
#endif

  WireInit(); //инициализация шины Wire
  IndiInit(); //инициализация индикаторов

  if (testRTC()) buzz_pulse(RTC_ERROR_SOUND_FREQ, RTC_ERROR_SOUND_TIME); //сигнал ошибки модуля часов
  if (!RIGHT_CHK) testLamp(); //если правая кнопка зажата запускаем тест системы

  randomSeed(timeRTC.s * (timeRTC.m + timeRTC.h) + timeRTC.DD * timeRTC.MM); //радомный сид для глюков
  _tmrGlitch = random(GLITCH_MIN, GLITCH_MAX); //находим рандомное время появления глюка
  changeBright(); //установка яркости от времени суток
  checkAlarms(); //проверка будильников
  //----------------------------------Главная----------------------------------
  for (;;) //главная
  {
    dataUpdate(); //обработка данных
    timerWarn(); //тревога таймера
    alarmWarn(); //тревога будильника
    dotFlash(); //мигаем точками
    mainScreen(); //главный экран
  }
  return 0; //конец
}
//---------------------------------------Прерывание от RTC----------------------------------------------
ISR(INT0_vect) //внешнее прерывание на пине INT0 - считаем секунды с RTC
{
  tick_sec++; //прибавляем секунду
}
//---------------------------------Прерывание сигнала для пищалки---------------------------------------
ISR(TIMER2_COMPB_vect) //прерывание сигнала для пищалки
{
  if (cnt_freq > 255) cnt_freq -= 255; //считаем циклы полуволны
  else if (cnt_freq) { //если остался хвост
    OCR2B = cnt_freq; //устанавливаем хвост
    cnt_freq = 0; //сбрасываем счетчик циклов полуволны
  }
  else { //если циклы полуволны кончились
    OCR2B = 255; //устанавливаем COMB в начало
    cnt_freq = tmr_score; //устанавливаем циклов полуволны
    BUZZ_INV; //инвертируем бузер
    if (!--cnt_puls) { //считаем циклы времени работы бузера
      BUZZ_OFF; //если циклы кончились, выключаем бузер
      TIMSK2 &= ~(0x01 << OCIE2B); //выключаем таймер
    }
  }
}
//---------------------------------Инициализация АЦП----------------------------------------------------
void initADC(void) //инициализация АЦП
{
  ADCSRA = (0x01 << ADEN) | (0x01 << ADPS0) | (0x01 << ADPS2); //настройка АЦП
  ADMUX = (0x01 << REFS0) | (0x01 << ADLAR) | ANALOG_BTN_PIN; //настройка мультиплексатора АЦП
}
//-----------------------------Инициализация будильника------------------------------------------------
void initAlarm(void) //инициализация будильника
{
  if (!alarms_num) newAlarm(); //создать новый будильник
  else if (alarms_num > 1) { //если будильников в памяти больше одного
    alarms_num = 1; //оставляем один будильник
    EEPROM_UpdateByte(EEPROM_BLOCK_ALARM, alarms_num); //записываем будильник в память
  }
}
//----------------------------------Отключение uart----------------------------------------------------
void uartDisable(void) //отключение uart
{
  UCSR0B = 0; //выключаем UART
  PRR |= (0x01 << PRUSART0); //выключаем питание UART
}
//------------------------------------Проверка системы-------------------------------------------------
void testLamp(void) //проверка системы
{
#if BACKL_WS2812B
  setLedBright(BACKL_BRIGHT); //устанавливаем максимальную яркость
#else
  OCR2A = BACKL_BRIGHT; //если посветка статичная, устанавливаем яркость
#endif
  dotSetBright(DOT_BRIGHT); //установка яркости точек
  while (1) {
    for (byte indi = 0; indi < LAMP_NUM; indi++) {
      indiClr(); //очистка индикаторов
      for (byte digit = 0; digit < 10; digit++) {
        indiPrintNum(digit, indi); //отрисовываем цифру
#if BACKL_WS2812B
        setLedColor(indi, (digit < 7) ? (digit + 1) : (digit - 6)); //отправляем статичный цвет
#endif
        for (_timer_ms[TMR_MS] = TEST_LAMP_TIME; _timer_ms[TMR_MS];) { //ждем
          dataUpdate(); //обработка данных
          MELODY_PLAY(0); //воспроизводим мелодию
          if (check_keys()) return; //возврат если нажата кнопка
        }
      }
#if BACKL_WS2812B
      setLedColor(indi, 0); //очищаем светодиод
#endif
    }
  }
}
//------------------------Проверка модуля часов реального времени--------------------------------------
boolean testRTC(void) //проверка модуля часов реального времени
{
  if (disble32K()) return 1; //отключение вывода 32K
  if (setSQW()) return 1; //установка SQW на 1Гц

  EICRA = (0x01 << ISC01); //настраиваем внешнее прерывание по спаду импульса на INT0
  EIFR |= (0x01 << INTF0); //сбрасываем флаг прерывания INT0

  for (_timer_ms[TMR_MS] = TEST_SQW_TIME; !(EIFR & (0x01 << INTF0)) && _timer_ms[TMR_MS];) { //ждем сигнала от SQW
    for (; tick_ms; tick_ms--) { //если был тик, обрабатываем данные
      if (_timer_ms[TMR_MS] > MS_PERIOD) _timer_ms[TMR_MS] -= MS_PERIOD; //если таймер больше периода
      else if (_timer_ms[TMR_MS]) _timer_ms[TMR_MS] = 0; //иначе сбрасываем таймер
    }
  }

  if (getTime()) return 1; //считываем время из RTC
  if (getOSF()) { //если пропадало питание
    EEPROM_ReadBlock((uint16_t)&timeRTC, EEPROM_BLOCK_TIME, sizeof(timeRTC)); //считываем дату и время из памяти
    sendTime(); //отправить время в RTC
  }

  if (EIFR & (0x01 << INTF0)) { //если был сигнал с SQW
    EIFR |= (0x01 << INTF0); //сбрасываем флаг прерывания INT0
    EIMSK = (0x01 << INT0); //разрешаем внешнее прерывание INT0
  }
  else return 1; //иначе выдаем ошибку

  return 0; //возвращаем статус "ок"
}
//------------------------Проверка данных в памяти--------------------------------------------------
boolean checkData(uint8_t size, uint8_t cell, uint8_t cellCRC) //проверка данных в памяти
{
  uint8_t crc = 0;
  for (uint8_t n = 0; n < size; n++) checkCRC(&crc, EEPROM_ReadByte(cell + n));
  if (crc != EEPROM_ReadByte(cellCRC)) return 1;
  return 0;
}
//------------------------Обновление данных в памяти--------------------------------------------------
void updateData(uint8_t* str, uint8_t size, uint8_t cell, uint8_t cellCRC) //обновление данных в памяти
{
  uint8_t crc = 0;
  for (uint8_t n = 0; n < size; n++) checkCRC(&crc, str[n]);
  EEPROM_UpdateBlock((uint16_t)str, cell, size);
  EEPROM_UpdateByte(cellCRC, crc);
}
//------------------------Сверка контрольной суммы--------------------------------------------------
void checkCRC(uint8_t* crc, uint8_t data) //сверка контрольной суммы
{
  for (uint8_t i = 0; i < 8; i++) { //считаем для всех бит
    *crc = ((*crc ^ data) & 0x01) ? (*crc >> 0x01) ^ 0x8C : (*crc >> 0x01); //рассчитываем значение
    data >>= 0x01; //сдвигаем буфер
  }
}
//------------------------Проверка контрольной суммы настроек-----------------------------------------
boolean checkSettingsCRC(void) //проверка контрольной суммы настроек
{
  uint8_t CRC = 0;

  for (uint8_t i = 0; i < sizeof(timeRTC); i++) checkCRC(&CRC, *((uint8_t*)&timeRTC + i));
  for (uint8_t i = 0; i < sizeof(mainSettings); i++) checkCRC(&CRC, *((uint8_t*)&mainSettings + i));
  for (uint8_t i = 0; i < sizeof(fastSettings); i++) checkCRC(&CRC, *((uint8_t*)&fastSettings + i));

  if (EEPROM_ReadByte(EEPROM_BLOCK_CRC) == CRC) return 0;
  else EEPROM_UpdateByte(EEPROM_BLOCK_CRC, CRC);
  return 1;
}
//--------------------------------Генерация частот бузера-----------------------------------------------
void buzz_pulse(uint16_t freq, uint16_t time) //генерация частоты бузера (частота 10..10000, длительность мс.)
{
  cnt_puls = ((uint32_t)freq * (uint32_t)time) / 500; //пересчитываем частоту и время в циклы таймера
  cnt_freq = tmr_score = (1000000 / freq); //пересчитываем частоту в циклы полуволны
  OCR2B = 255; //устанавливаем COMB в начало
  TIMSK2 |= (0x01 << OCIE2B); //запускаем таймер
}
//---------------------------------Воспроизведение мелодии-----------------------------------------------
void _melody_chart(uint8_t melody) //воспроизведение мелодии
{
  if (!_timer_ms[TMR_MELODY]) { //если пришло время
    buzz_pulse(pgm_read_word((uint16_t*)(alarm_sound[melody][0] + (6 * semp))), pgm_read_word((uint16_t*)(alarm_sound[melody][0] + (6 * semp) + 2))); //запускаем звук с задоной частотой и временем
    _timer_ms[TMR_MELODY] = pgm_read_word((uint16_t*)(alarm_sound[melody][0] + (6 * semp) + 4)); //устанавливаем паузу перед воспроизведением нового звука
    if (++semp > alarm_sound[melody][1] - 1) semp = 0; //переключпем на следующий семпл
  }
}
//----------------------------------Проверка будильников----------------------------------------------------
void checkAlarms(void) //проверка будильников
{
  alarmEnabled = 0; //сбрасываем флаг включенного будильника
  for (uint8_t alm = 0; alm < alarms_num; alm++) { //опрашиваем все будильники
    if (alarmRead(alm, 2)) { //если будильник включен
      alarmEnabled = 1; //устанавливаем флаг включенного будильника
      if (timeRTC.h == alarmRead(alm, 0) && timeRTC.m == alarmRead(alm, 1) && (alarmRead(alm, 2) < 3 || (alarmRead(alm, 2) == 3 && timeRTC.DW < 6) || (alarmRead(alm, 3) & (0x01 << timeRTC.DW)))) {
        alarm = alm + 1; //устанавливаем флаг тревоги
        return; //выходим
      }
    }
  }
}
//----------------------------Обновление данных будильников--------------------------------------------------
void alarmDataUpdate(void) //обновление данных будильников
{
  if (alarm) { //если тревога активна
    if (++minsAlarm >= ALARM_TIMEOUT) { //если пришло время выключить будильник
      alarmReset(); //сброс будильника
      MELODY_RESET; //сброс позиции мелодии
      return; //выходим
    }

    if (ALARM_WAINT && alarmWaint) { //если будильник в режиме ожидания
      if (++minsAlarmWaint >= ALARM_WAINT) { //если пришло время повторно включить звук
        alarmWaint = 0; //сбрасываем флаг ожидания
        minsAlarmWaint = 0; //сбрасываем таймер ожидания
      }
    }
    else if (ALARM_TIMEOUT_SOUND) { //если таймаут тревоги включен
      if (++minsAlarmSound >= ALARM_TIMEOUT_SOUND) { //если пришло время выключить тревогу
        if (ALARM_WAINT) { //если время ожидания включено
          alarmWaint = 1; //устанавливаем флаг ожидания тревоги
          minsAlarmSound = 0; //сбрасываем таймер таймаута тревоги
        }
        else alarmReset(); //сброс будильника
        MELODY_RESET; //сброс позиции мелодии
      }
    }
  }
  else checkAlarms(); //иначе проверяем будильники на совподение
}
//----------------------------------Тревога будильника---------------------------------------------------------
void alarmWarn(void) //тревога будильника
{
  if (alarm && !alarmWaint) { //если флаг установлен флаг тревоги и флаг ожидания очещен
    boolean blink_data = 0; //флаг мигания индикаторами
    uint8_t soundNum = alarmRead(alarm - 1, 4); //считываем мелодию сигнала тревоги
    while (1) {
      dataUpdate(); //обработка данных

      if (!alarm || alarmWaint) { //если тревога сброшена
        _animShow = 0; //сбросить флаг анимации
        return; //выходим
      }

      MELODY_PLAY(soundNum); //воспроизводим мелодию

      if (!_timer_ms[TMR_MS]) { //если прошло пол секунды
        _timer_ms[TMR_MS] = ALM_BLINK_TIME; //устанавливаем таймер

        switch (blink_data) {
          case 0:
            indiClr(); //очистка индикаторов
            dotSetBright(0); //выключаем точки
            break;
          case 1:
            indiPrintNum((mainSettings.timeFormat) ? get_12h(timeRTC.h) : timeRTC.h, 0, 2, 0); //вывод часов
            indiPrintNum(timeRTC.m, 2, 2, 0); //вывод минут
            indiPrintNum(timeRTC.s, 4, 2, 0); //вывод секунд
            dotSetBright(dotMaxBright); //включаем точки
            break;
        }
        blink_data = !blink_data; //мигаем временем
      }

      switch (check_keys()) {
        case LEFT_KEY_PRESS: //клик левой кнопкой
        case RIGHT_KEY_PRESS: //клик правой кнопкой
        case SET_KEY_PRESS: //клик средней кнопкой
        case ADD_KEY_PRESS: //клик дополнительной кнопкой
          if (ALARM_WAINT) { //если есть время ожидания
            alarmWaint = 1; //устанавливаем флаг ожидания
            minsAlarmSound = 0; //сбрасывем таймер ожидания
          }
          else {
            buzz_pulse(ALM_OFF_SOUND_FREQ, ALM_OFF_SOUND_TIME); //звук выключения будильника
            alarmReset(); //сброс будильника
          }
          MELODY_RESET; //сброс позиции мелодии
          _animShow = 0; //сбросить флаг анимации
          _sec = 0; //обновление экрана
          return; //выходим

        case LEFT_KEY_HOLD: //удержание левой кнопки
        case RIGHT_KEY_HOLD: //удержание правой кнопки
        case SET_KEY_HOLD: //удержание средней кнопки
        case ADD_KEY_HOLD: //удержание дополнительной кнопки
          buzz_pulse(ALM_OFF_SOUND_FREQ, ALM_OFF_SOUND_TIME); //звук выключения будильника
          alarmReset(); //сброс будильника
          MELODY_RESET; //сброс позиции мелодии
          _animShow = 0; //сбросить флаг анимации
          _sec = 0; //обновление экрана
          return; //выходим
      }
    }
  }
}
//----------------------------------Сброс будильника---------------------------------------------------------
void alarmReset(void) //сброс будильника
{
  if (alarmRead(alarm - 1, 2) == 1) EEPROM_UpdateByte(EEPROM_BLOCK_ALARM_DATA + ((alarm - 1) * 5) + 2, 0); //если был установлен режим одиночный то выключаем будильник
  checkAlarms(); //проверка будильников
  alarmWaint = 0; //сбрасываем флаг ожидания
  minsAlarm = 0; //сбрасываем таймер отключения будильника
  minsAlarmWaint = 0; //сбрасываем таймер ожидания повторного включения тревоги
  minsAlarmSound = 0; //сбрасываем таймер отключения звука
  alarm = 0; //сбрасываем флаг тревоги
}
//----------------------------------Получить основные данные будильника---------------------------------------------------------
uint8_t alarmRead(uint8_t almNum, uint8_t almData) //получить основные данные будильника
{
  return EEPROM_ReadByte(EEPROM_BLOCK_ALARM_DATA + (almNum * 5) + almData); //возвращаем запрошеный байт
}
//----------------------------------Получить блок основных данных будильника---------------------------------------------------------
void alarmReadBlock(uint8_t almNum, uint8_t* data) //получить блок основных данных будильника
{
  for (uint8_t i = 0; i < 5; i++) data[i] = (almNum) ? EEPROM_ReadByte(EEPROM_BLOCK_ALARM_DATA + ((almNum - 1) * 5) + i) : 0; //считываем блок данных
}
//----------------------------------Записать блок основных данных будильника---------------------------------------------------------
void alarmWriteBlock(uint8_t almNum, uint8_t* data) //записать блок основных данных будильника
{
  if (!almNum) return; //если нет ни одного будильника - выходим
  for (uint8_t i = 0; i < 5; i++) EEPROM_UpdateByte(EEPROM_BLOCK_ALARM_DATA + ((almNum - 1) * 5) + i, data[i]); //записываем блок данных
}
//---------------------Создать новый будильник---------------------------------------------------------
void newAlarm(void) //создать новый будильник
{
  if (alarms_num < MAX_ALARMS) { //если новый будильник меньше максимума
    EEPROM_UpdateByte(EEPROM_BLOCK_ALARM_DATA + (alarms_num * 5), DEFAULT_ALARM_TIME_HH); //устанавливаем час по умолчанию
    EEPROM_UpdateByte(EEPROM_BLOCK_ALARM_DATA + (alarms_num * 5) + 1, DEFAULT_ALARM_TIME_MM); //устанавливаем минуты по умолчанию
    EEPROM_UpdateByte(EEPROM_BLOCK_ALARM_DATA + (alarms_num * 5) + 2, DEFAULT_ALARM_MODE); //устанавливаем режим по умолчанию
    EEPROM_UpdateByte(EEPROM_BLOCK_ALARM_DATA + (alarms_num * 5) + 3, 0); //устанавливаем дни недели по умолчанию
    EEPROM_UpdateByte(EEPROM_BLOCK_ALARM_DATA + (alarms_num * 5) + 4, DEFAULT_ALARM_SOUND); //устанавливаем мелодию по умолчанию
    EEPROM_UpdateByte(EEPROM_BLOCK_ALARM, ++alarms_num); //записываем количество будильников в память
  }
}
//---------------------Удалить будильник---------------------------------------------------------
void delAlarm(uint8_t alarm) //удалить будильник
{
  if (alarms_num) { //если будильник доступен
    for (uint8_t start = alarm; start < alarms_num; start++) { //перезаписываем массив будильников
      EEPROM_UpdateByte(EEPROM_BLOCK_ALARM_DATA + (alarms_num * 5), EEPROM_ReadByte(EEPROM_BLOCK_ALARM_DATA + ((alarms_num + 1) * 5)));
      EEPROM_UpdateByte(EEPROM_BLOCK_ALARM_DATA + (alarms_num * 5) + 1, EEPROM_ReadByte(EEPROM_BLOCK_ALARM_DATA + ((alarms_num + 1) * 5) + 1));
      EEPROM_UpdateByte(EEPROM_BLOCK_ALARM_DATA + (alarms_num * 5) + 2, EEPROM_ReadByte(EEPROM_BLOCK_ALARM_DATA + ((alarms_num + 1) * 5) + 2));
      EEPROM_UpdateByte(EEPROM_BLOCK_ALARM_DATA + (alarms_num * 5) + 3, EEPROM_ReadByte(EEPROM_BLOCK_ALARM_DATA + ((alarms_num + 1) * 5) + 3));
      EEPROM_UpdateByte(EEPROM_BLOCK_ALARM_DATA + (alarms_num * 5) + 4, EEPROM_ReadByte(EEPROM_BLOCK_ALARM_DATA + ((alarms_num + 1) * 5) + 4));
    }
    EEPROM_UpdateByte(EEPROM_BLOCK_ALARM, --alarms_num); //записываем количество будильников в память
  }
}
//----------------------------------Обработка данных---------------------------------------------------------
void dataUpdate(void) //обработка данных
{
  static uint32_t timeNotRTC; //счетчик реального времени
  static uint16_t timerCorrect; //остаток для коррекции времени
#if BACKL_WS2812B
  backlEffect(); //анимация подсветки
#else
  backlFlash(); //"дыхание" подсветки
#endif

  for (; tick_ms > 0; tick_ms--) { //если был тик, обрабатываем данные
    switch (btn_state) { //таймер опроса кнопок
      case 0: if (btn_check) btn_tmr++; break; //считаем циклы
      case 1: if (btn_tmr) btn_tmr--; break; //убираем дребезг
    }

    if (!EIMSK) { //если внешние часы не обнаружены
      timeNotRTC += mainSettings.timePeriod; //добавляем ко времени период таймера
      if (timeNotRTC > 1000000UL) { //если прошла секунда
        timeNotRTC -= 1000000UL; //оставляем остаток
        tick_sec++; //прибавляем секунду
      }
    }

    timerCorrect += mainSettings.timePeriod % 1000; //остаток для коррекции
    uint16_t msDec = (mainSettings.timePeriod + timerCorrect) / 1000; //находим целые мс
    for (uint8_t tm = 0; tm < TIMERS_NUM; tm++) { //опрашиваем все таймеры
      if (_timer_ms[tm] > msDec) _timer_ms[tm] -= msDec; //если таймер больше периода
      else if (_timer_ms[tm]) _timer_ms[tm] = 0; //иначе сбрасываем таймер
    }
    if (timerCorrect >= 1000) timerCorrect -= 1000; //если коррекция больше либо равна 1 мс
  }

  for (; tick_sec > 0; tick_sec--) { //если был тик, обрабатываем данные
    //счет времени
    if (++timeRTC.s > 59) { //секунды
      timeRTC.s = 0;
      if (++timeRTC.m > 59) { //минуты
        timeRTC.m = 0;
        if (++timeRTC.h > 23) { //часы
          timeRTC.h = 0;
          if (++timeRTC.DW > 7) timeRTC.DW = 1; //день недели
          if (++timeRTC.DD > maxDays()) { //дата
            timeRTC.DD = 1;
            if (++timeRTC.MM > 12) { //месяц
              timeRTC.MM = 1;
              if (++timeRTC.YY > 2050) { //год
                timeRTC.YY = 2021;
              }
            }
          }
        }
        hourSound(); //звук смены часа
        changeBright(); //установка яркости от времени суток
      }
      _tmrBurn++; //прибавляем минуту к таймеру антиотравления
      _animShow = 1; //показать анимацию переключения цифр
      alarmDataUpdate(); //проверка будильников
    }
    switch (timerMode) {
      case 1: if (timerCnt != 65535) timerCnt++; break;
      case 2: if (timerCnt) timerCnt--; break;
    }
    _sec = _dot = 0; //очищаем флаги секунды и точек
  }
}
//------------------------------------Звук смены часа------------------------------------
void hourSound(void) //звук смены часа
{
  if (!alarm || alarmWaint) { //если будильник не работает
    if ((mainSettings.timeHour[1] > mainSettings.timeHour[0] && timeRTC.h < mainSettings.timeHour[1] && timeRTC.h >= mainSettings.timeHour[0]) ||
        (mainSettings.timeHour[1] < mainSettings.timeHour[0] && (timeRTC.h < mainSettings.timeHour[1] || timeRTC.h >= mainSettings.timeHour[0]))) {
      buzz_pulse(HOUR_SOUND_FREQ, HOUR_SOUND_TIME); //звук смены часа
    }
  }
}
//------------------------------------Имитация глюков------------------------------------
void glitchMode(void) //имитация глюков
{
  if (mainSettings.glitchMode) { //если глюки включены
    if (!_tmrGlitch-- && timeRTC.s > 7 && timeRTC.s < 55) { //если пришло время
      boolean indiState = 0; //состояние индикатора
      uint8_t glitchCounter = random(GLITCH_NUM_MIN, GLITCH_NUM_MAX); //максимальное количество глюков
      uint8_t glitchIndic = random(0, LAMP_NUM); //номер индикатора
      uint8_t indiSave = indiGet(glitchIndic); //сохраняем текущую цифру в индикаторе
      while (!check_keys()) {
        dataUpdate(); //обработка данных
        dotFlash(); //мигаем точками

        if (!_timer_ms[TMR_GLITCH]) { //если таймер истек
          if (!indiState) indiClr(glitchIndic); //выключаем индикатор
          else indiSet(indiSave, glitchIndic); //установка индикатора
          indiState = !indiState; //меняем состояние глюка лампы
          _timer_ms[TMR_GLITCH] = random(1, 6) * 20; //перезапускаем таймер глюка
          if (!glitchCounter--) break; //выходим если закончились глюки
        }
      }
      _tmrGlitch = random(GLITCH_MIN, GLITCH_MAX); //находим рандомное время появления глюка
      indiSet(indiSave, glitchIndic); //восстанавливаем состояние индикатора
    }
  }
}
//-----------------------Проверка аналоговой кнопки-----------------------------------------------
boolean checkKeyADC(uint8_t minADC, uint8_t maxADC) //проверка аналоговой кнопки
{
  ADCSRA |= (1 << ADSC); //запускаем преобразование
  while (ADCSRA & (1 << ADSC)); //ждем окончания преобразования
  return !(minADC < ADCH && ADCH <= maxADC); //возвращаем результат опроса
}
//-----------------------------Проверка кнопок----------------------------------------------------
uint8_t check_keys(void) //проверка кнопок
{
  static uint8_t btn_set; //флаг признака действия
  static uint8_t btn_switch; //флаг мультиопроса кнопок

  switch (btn_switch) { //переключаемся в зависимости от состояния мультиопроса
    case 0:
      if (!SET_CHK) { //если нажата кл. ок
        btn_switch = 1; //выбираем клавишу опроса
        btn_state = 0; //обновляем текущее состояние кнопки
      }
      else if (!LEFT_CHK) { //если нажата левая кл.
        btn_switch = 2; //выбираем клавишу опроса
        btn_state = 0; //обновляем текущее состояние кнопки
      }
      else if (!RIGHT_CHK) { //если нажата правая кл.
        btn_switch = 3; //выбираем клавишу опроса
        btn_state = 0; //обновляем текущее состояние кнопки
      }
      else if (!ADD_CHK) { //если нажата дополнительная кл.
        btn_switch = 4; //выбираем клавишу опроса
        btn_state = 0; //обновляем текущее состояние кнопки
      }
      else btn_state = 1; //обновляем текущее состояние кнопки
      break;
    case 1: btn_state = SET_CHK; break; //опрашиваем клавишу ок
    case 2: btn_state = LEFT_CHK; break; //опрашиваем левую клавишу
    case 3: btn_state = RIGHT_CHK; break; //опрашиваем правую клавишу
    case 4: btn_state = ADD_CHK; break; //опрашиваем дополнительную клавишу
  }

  switch (btn_state) { //переключаемся в зависимости от состояния клавиши
    case 0:
      if (btn_check) { //если разрешена провекрка кнопки
        if (btn_tmr > BTN_HOLD_TICK) { //если таймер больше длительности удержания кнопки
          btn_tmr = BTN_GIST_TICK; //сбрасываем таймер на антидребезг
          btn_set = 2; //поднимаем признак удержания
          btn_check = 0; //запрещем проврку кнопки
        }
      }
      break;

    case 1:
      if (btn_tmr > BTN_GIST_TICK) { //если таймер больше времени антидребезга
        btn_tmr = BTN_GIST_TICK; //сбрасываем таймер на антидребезг
        btn_set = 1; //если не спим и если подсветка включена, поднимаем признак нажатия
        btn_check = 0; //запрещем проврку кнопки
        if (mainSettings.knock_sound) buzz_pulse(KNOCK_SOUND_FREQ, KNOCK_SOUND_TIME); //щелчок пищалкой
      }
      else if (!btn_tmr) {
        btn_check = 1; //разрешаем проврку кнопки
        btn_switch = 0; //сбрасываем мультиопрос кнопок
      }
      break;
  }

  switch (btn_set) { //переключаемся в зависимости от признака нажатия
    case 0: return KEY_NULL; //клавиша не нажата, возвращаем 0
    case 1:
      btn_set = 0; //сбрасываем признак нажатия
      switch (btn_switch) { //переключаемся в зависимости от состояния мультиопроса
        case 1: return SET_KEY_PRESS; //возвращаем клик средней кнопкой
        case 2: return LEFT_KEY_PRESS; //возвращаем клик левой кнопкой
        case 3: return RIGHT_KEY_PRESS; //возвращаем клик правой кнопкой
        case 4: return ADD_KEY_PRESS; //возвращаем клик дополнительной кнопкой
      }
      break;

    case 2:
      btn_set = 0; //сбрасываем признак нажатия
      switch (btn_switch) { //переключаемся в зависимости от состояния мультиопроса
        case 1: return SET_KEY_HOLD; //возвращаем удержание средней кнопки
        case 2: return LEFT_KEY_HOLD; //возвращаем удержание левой кнопки
        case 3: return RIGHT_KEY_HOLD; //возвращаем удержание правой кнопки
        case 4: return ADD_KEY_HOLD; //возвращаем удержание дополнительной кнопки
      }
      break;
  }
  return KEY_NULL;
}
//----------------------------Настройки времени----------------------------------
void settings_time(void) //настройки времени
{
  uint8_t cur_mode = 0; //текущий режим
  uint8_t time_out = 0; //таймаут автовыхода
  boolean blink_data = 0; //мигание сигментами

  indiClr(); //очищаем индикаторы
  dotSetBright(dotMaxBright); //включаем точки

  //настройки
  while (1) {
    dataUpdate(); //обработка данных

    if (!_sec) {
      _sec = 1;
      if (++time_out >= SETTINGS_TIMEOUT) return;
    }

    if (!_timer_ms[TMR_MS]) { //если прошло пол секунды
      _timer_ms[TMR_MS] = SETTINGS_BLINK_TIME; //устанавливаем таймер

      indiClr(); //очистка индикаторов
      indiPrintNum(cur_mode + 1, 5); //режим
      switch (cur_mode) {
        case 0:
        case 1:
          if (!blink_data || cur_mode != 0) indiPrintNum(timeRTC.h, 0, 2, 0); //вывод часов
          if (!blink_data || cur_mode != 1) indiPrintNum(timeRTC.m, 2, 2, 0); //вывод минут
          break;
        case 2:
        case 3:
          if (!blink_data || cur_mode != 2) indiPrintNum(timeRTC.DD, 0, 2, 0); //вывод даты
          if (!blink_data || cur_mode != 3) indiPrintNum(timeRTC.MM, 2, 2, 0); //вывод месяца
          break;
        case 4:
          if (!blink_data) indiPrintNum(timeRTC.YY, 0); //вывод года
          break;
      }
      blink_data = !blink_data; //мигание сигментами
    }

    //+++++++++++++++++++++  опрос кнопок  +++++++++++++++++++++++++++
    switch (check_keys()) {
      case LEFT_KEY_PRESS: //клик левой кнопкой
        switch (cur_mode) {
          //настройка времени
          case 0: if (timeRTC.h > 0) timeRTC.h--; else timeRTC.h = 23; timeRTC.s = 0; break; //часы
          case 1: if (timeRTC.m > 0) timeRTC.m--; else timeRTC.m = 59; timeRTC.s = 0; break; //минуты

          //настройка даты
          case 2: if (timeRTC.DD > 1) timeRTC.DD--; else timeRTC.DD = maxDays(); break; //день
          case 3: //месяц
            if (timeRTC.MM > 1) timeRTC.MM--;
            else timeRTC.MM = 12;
            if (timeRTC.DD > maxDays()) timeRTC.DD = maxDays();
            break;

          //настройка года
          case 4: if (timeRTC.YY > 2021) timeRTC.YY--; else timeRTC.YY = 2050; break; //год
        }
        _timer_ms[TMR_MS] = time_out = blink_data = 0; //сбрасываем флаги
        break;

      case RIGHT_KEY_PRESS: //клик правой кнопкой
        switch (cur_mode) {
          //настройка времени
          case 0: if (timeRTC.h < 23) timeRTC.h++; else timeRTC.h = 0; timeRTC.s = 0; break; //часы
          case 1: if (timeRTC.m < 59) timeRTC.m++; else timeRTC.m = 0; timeRTC.s = 0; break; //минуты

          //настройка даты
          case 2: if (timeRTC.DD < maxDays()) timeRTC.DD++; else timeRTC.DD = 1; break; //день
          case 3: //месяц
            if (timeRTC.MM < 12) timeRTC.MM++;
            else timeRTC.MM = 1;
            if (timeRTC.DD > maxDays()) timeRTC.DD = maxDays();
            break;

          //настройка года
          case 4: if (timeRTC.YY < 2050) timeRTC.YY++; else timeRTC.YY = 2021; break; //год
        }
        _timer_ms[TMR_MS] = time_out = blink_data = 0; //сбрасываем флаги
        break;

      case SET_KEY_PRESS: //клик средней кнопкой
        if (cur_mode < 4) cur_mode++; else cur_mode = 0;
        if (cur_mode != 4) dotSetBright(dotMaxBright); //включаем точки
        else dotSetBright(0); //выключаем точки
        _timer_ms[TMR_MS] = time_out = blink_data = 0; //сбрасываем флаги
        break;

      case SET_KEY_HOLD: //удержание средней кнопки
        sendTime(); //отправить время в RTC
        changeBright(); //установка яркости от времени суток
        updateData((uint8_t*)&timeRTC, sizeof(timeRTC), EEPROM_BLOCK_TIME, EEPROM_BLOCK_CRC_TIME); //записываем дату и время в память
        return;
    }
  }
}
//-----------------------------Настройка будильника------------------------------------
void settings_singleAlarm(void) //настройка будильника
{
  uint8_t alarm[5]; //массив данных о будильнике
  uint8_t cur_mode = 0; //текущий режим
  uint8_t cur_day = 1; //текущий день недели
  boolean blink_data = 0; //мигание сигментами
  uint8_t time_out = 0; //таймаут автовыхода
  boolean cur_indi = 0; //текущий индикатор

  indiClr(); //очищаем индикаторы
  dotSetBright(dotMaxBright); //выключаем точки

  alarmReset(); //сброс будильника
  alarmReadBlock(1, alarm); //читаем блок данных

  while (1) {
    dataUpdate(); //обработка данных

    if (!_sec) {
      _sec = 1;
      if (++time_out >= SETTINGS_TIMEOUT) {
        MELODY_RESET; //сброс воспроизведения мелодии
        checkAlarms(); //проверка будильников
        return; //выходим по тайм-ауту
      }
    }

    if (cur_mode == 3) MELODY_PLAY(alarm[4]); //воспроизводим мелодию

    if (!_timer_ms[TMR_MS]) { //если прошло пол секунды
      _timer_ms[TMR_MS] = SETTINGS_BLINK_TIME; //устанавливаем таймер

      indiClr(); //очистка индикаторов
      indiPrintNum(cur_mode + 1, 5); //режим
      switch (cur_mode) {
        case 0:
          if (!blink_data || cur_indi) indiPrintNum(alarm[0], 0, 2, 0); //вывод часов
          if (!blink_data || !cur_indi) indiPrintNum(alarm[1], 2, 2, 0); //вывод минут
          break;
        case 1:
        case 2:
          if (!blink_data || cur_mode != 1) indiPrintNum(alarm[2], 0); //вывод режима
          if (alarm[2] > 3) {
            if (!blink_data || cur_mode != 2 || cur_indi) indiPrintNum(cur_day, 2); //вывод дня недели
            if (!blink_data || cur_mode != 2 || !cur_indi) indiPrintNum((alarm[3] >> cur_day) & 0x01, 3); //вывод установки
          }
          break;
        case 3:
          if (!blink_data) indiPrintNum(alarm[4] + 1, 3); //вывод номера мелодии
          break;
      }
      blink_data = !blink_data; //мигание сигментами
    }

    //+++++++++++++++++++++  опрос кнопок  +++++++++++++++++++++++++++
    switch (check_keys()) {
      case LEFT_KEY_HOLD: //удержание левой кнопки
        if (cur_mode > 0) cur_mode--; else cur_mode = 3; //переключение пунктов

        switch (cur_mode) {
          case 0: dotSetBright(dotMaxBright); break; //включаем точки
          case 2: if (alarm[2] < 4) cur_mode = 1; break; //если нет дней недели
          default: dotSetBright(0); break; //выключаем точки
        }

        MELODY_RESET; //сбрасываем мелодию
        cur_indi = 0; //сбрасываем текущий индикатор
        blink_data = 0; //сбрасываем флаги
        _timer_ms[TMR_MS] = time_out = 0; //сбрасываем таймеры
        break;
      case LEFT_KEY_PRESS: //клик левой кнопкой
        switch (cur_mode) {
          //настройка времени будильника
          case 0:
            switch (cur_indi) {
              case 0: if (alarm[0] > 0) alarm[0]--; else alarm[0] = 23; break; //часы
              case 1: if (alarm[1] > 0) alarm[1]--; else alarm[1] = 59; break; //минуты
            }
            break;
          //настройка режима будильника
          case 1: if (alarm[2] > 0) alarm[2]--; else alarm[2] = 4; break; //режим
          case 2:
            switch (cur_indi) {
              case 0: if (cur_day > 1) cur_day--; else cur_day = 7; break; //день недели
              case 1: alarm[3] &= ~(0x01 << cur_day); break; //установка
            }
            break;
          //настройка мелодии будильника
          case 3: if (alarm[4] > 0) alarm[4]--; else alarm[4] = (sizeof(alarm_sound) >> 2) - 1; MELODY_RESET; break; //мелодия
        }
        blink_data = 0; //сбрасываем флаги
        _timer_ms[TMR_MS] = time_out = 0; //сбрасываем таймеры
        break;
      case RIGHT_KEY_PRESS: //клик правой кнопкой
        switch (cur_mode) {
          //настройка времени будильника
          case 0:
            switch (cur_indi) {
              case 0: if (alarm[0] < 23) alarm[0]++; else alarm[0] = 0; break; //часы
              case 1: if (alarm[1] < 59) alarm[1]++; else alarm[1] = 0; break; //минуты
            }
            break;
          //настройка режима будильника
          case 1: if (alarm[2] < 4) alarm[2]++; else alarm[2] = 0; break; //режим
          case 2:
            switch (cur_indi) {
              case 0: if (cur_day < 7) cur_day++; else cur_day = 1; break; //день недели
              case 1: alarm[3] |= (0x01 << cur_day); break; //установка
            }
            break;
          //настройка мелодии будильника
          case 3: if (alarm[4] < ((sizeof(alarm_sound) >> 2) - 1)) alarm[4]++; else alarm[4] = 0; MELODY_RESET; break; //мелодия
        }
        blink_data = 0; //сбрасываем флаги
        _timer_ms[TMR_MS] = time_out = 0; //сбрасываем таймеры
        break;
      case RIGHT_KEY_HOLD: //удержание правой кнопки
        if (cur_mode < 3) cur_mode++; else cur_mode = 0; //переключение пунктов

        switch (cur_mode) {
          case 0: dotSetBright(dotMaxBright); break; //включаем точки
          case 2: if (alarm[2] < 4) cur_mode = 3; break; //если нет дней недели
          default: dotSetBright(0); break; //выключаем точки
        }

        MELODY_RESET; //сбрасываем мелодию
        cur_indi = 0; //сбрасываем текущий индикатор
        blink_data = 0; //сбрасываем флаги
        _timer_ms[TMR_MS] = time_out = 0; //сбрасываем таймеры
        break;
      case SET_KEY_PRESS: //клик средней кнопкой
        cur_indi = !cur_indi;
        blink_data = 0; //сбрасываем флаги
        _timer_ms[TMR_MS] = time_out = 0; //сбрасываем таймеры
        break;
      case SET_KEY_HOLD: //удержание средней кнопки
        alarmWriteBlock(1, alarm); //записать блок основных данных будильника и выйти
        checkAlarms(); //проверка будильников
        return;
    }
  }
}
//-----------------------------Настройка будильников------------------------------------
void settings_multiAlarm(void) //настройка будильников
{
  uint8_t alarm[5]; //массив данных о будильнике
  uint8_t cur_mode = 0; //текущий режим
  uint8_t cur_day = 1; //текущий день недели
  boolean blink_data = 0; //мигание сигментами
  uint8_t time_out = 0; //таймаут автовыхода
  uint8_t curAlarm = alarms_num > 0;

  indiClr(); //очищаем индикаторы
  dotSetBright(0); //выключаем точки

  alarmReset(); //сброс будильника
  alarmReadBlock(curAlarm, alarm); //читаем блок данных

  while (1) {
    dataUpdate(); //обработка данных

    if (!_sec) {
      _sec = 1;
      if (++time_out >= SETTINGS_TIMEOUT) {
        MELODY_RESET; //сброс воспроизведения мелодии
        checkAlarms(); //проверка будильников
        return; //выходим по тайм-ауту
      }
    }

    if (cur_mode == 6) MELODY_PLAY(alarm[4]); //воспроизводим мелодию

    if (!_timer_ms[TMR_MS]) { //если прошло пол секунды
      _timer_ms[TMR_MS] = SETTINGS_BLINK_TIME; //устанавливаем таймер

      indiClr(); //очистка индикаторов
      indiPrintNum(cur_mode + 1, 5); //режим
      switch (cur_mode) {
        case 0:
        case 1:
          if (!blink_data || cur_mode != 0) indiPrintNum(curAlarm, 0, 2, 0); //вывод номера будильника
          if ((!blink_data || cur_mode != 1) && curAlarm) indiPrintNum(alarm[2], 3); //вывод режима
          break;
        case 2:
        case 3:
          if (!blink_data || cur_mode != 2) indiPrintNum(alarm[0], 0, 2, 0); //вывод часов
          if (!blink_data || cur_mode != 3) indiPrintNum(alarm[1], 2, 2, 0); //вывод минут
          break;
        case 4:
        case 5:
        case 6:
          if (alarm[2] > 3) {
            if (!blink_data || cur_mode != 4) indiPrintNum(cur_day, 0); //вывод дня недели
            if (!blink_data || cur_mode != 5) indiPrintNum((alarm[3] >> cur_day) & 0x01, 1); //вывод установки
          }
          if (!blink_data || cur_mode != 6) indiPrintNum(alarm[4] + 1, 2, 2, 0); //вывод номера мелодии
          break;
      }
      blink_data = !blink_data; //мигание сигментами
    }

    //+++++++++++++++++++++  опрос кнопок  +++++++++++++++++++++++++++
    switch (check_keys()) {
      case LEFT_KEY_HOLD: //удержание левой кнопки
        switch (cur_mode) {
          case 0:
            if (curAlarm) { //если есть будильники в памяти
              delAlarm(curAlarm - 1); //удалить текущий будильник
              dotSetBright(dotMaxBright); //включаем точки
              for (_timer_ms[TMR_MS] = 500; _timer_ms[TMR_MS];) dataUpdate(); //обработка данных
              dotSetBright(0); //выключаем точки
              if (curAlarm > (alarms_num > 0)) curAlarm--; //убавляем номер текущего будильника
              else curAlarm = (alarms_num > 0);
              alarmReadBlock(curAlarm, alarm); //читаем блок данных
            }
            break;
          default:
            cur_mode = 2; //режим настройки времени
            dotSetBright(dotMaxBright); //включаем точки
            break;
        }
        blink_data = 0; //сбрасываем флаги
        _timer_ms[TMR_MS] = time_out = 0; //сбрасываем таймеры
        break;
      case LEFT_KEY_PRESS: //клик левой кнопкой
        switch (cur_mode) {
          case 0: if (curAlarm > (alarms_num > 0)) curAlarm--; else curAlarm = alarms_num; alarmReadBlock(curAlarm, alarm); break; //будильник
          case 1: if (alarm[2] > 0) alarm[2]--; else alarm[2] = 4; break; //режим

          //настройка времени будильника
          case 2: if (alarm[0] > 0) alarm[0]--; else alarm[0] = 23; break; //часы
          case 3: if (alarm[1] > 0) alarm[1]--; else alarm[1] = 59; break; //минуты

          //настройка режима будильника
          case 4: if (cur_day > 1) cur_day--; else cur_day = 7; break; //день недели
          case 5: alarm[3] &= ~(0x01 << cur_day); break; //установка
          case 6: if (alarm[4] > 0) alarm[4]--; else alarm[4] = (sizeof(alarm_sound) >> 2) - 1; MELODY_RESET; break; //мелодия
        }
        blink_data = 0; //сбрасываем флаги
        _timer_ms[TMR_MS] = time_out = 0; //сбрасываем таймеры
        break;
      case RIGHT_KEY_PRESS: //клик правой кнопкой
        switch (cur_mode) {
          case 0: if (curAlarm < alarms_num) curAlarm++; else curAlarm = (alarms_num > 0); alarmReadBlock(curAlarm, alarm); break; //будильник
          case 1: if (alarm[2] < 4) alarm[2]++; else alarm[2] = 0; break; //режим

          //настройка времени будильника
          case 2: if (alarm[0] < 23) alarm[0]++; else alarm[0] = 0; break; //часы
          case 3: if (alarm[1] < 59) alarm[1]++; else alarm[1] = 0; break; //минуты

          //настройка режима будильника
          case 4: if (cur_day < 7) cur_day++; else cur_day = 1; break; //день недели
          case 5: alarm[3] |= (0x01 << cur_day); break; //установка
          case 6: if (alarm[4] < ((sizeof(alarm_sound) >> 2) - 1)) alarm[4]++; else alarm[4] = 0; MELODY_RESET; break; //мелодия
        }
        blink_data = 0; //сбрасываем флаги
        _timer_ms[TMR_MS] = time_out = 0; //сбрасываем таймеры
        break;
      case RIGHT_KEY_HOLD: //удержание правой кнопки
        switch (cur_mode) {
          case 0:
            newAlarm(); //создать новый будильник
            dotSetBright(dotMaxBright); //включаем точки
            for (_timer_ms[TMR_MS] = 500; _timer_ms[TMR_MS];) dataUpdate(); //обработка данных
            dotSetBright(0); //выключаем точки
            curAlarm = alarms_num;
            alarmReadBlock(curAlarm, alarm); //читаем блок данных
            break;
          case 4:
          case 5: cur_mode = 6; break;
          default:
            cur_mode = (alarm[2] < 4) ? 6 : 4; //режим настройки функций
            dotSetBright(dotMaxBright); //включаем точки
            break;

        }
        blink_data = 0; //сбрасываем флаги
        _timer_ms[TMR_MS] = time_out = 0; //сбрасываем таймеры
        break;
      case SET_KEY_PRESS: //клик средней кнопкой
        switch (cur_mode) {
          case 0: if (curAlarm) cur_mode = 1; break; //если есть будильники в памяти, перейти к настройке режима
          case 1: cur_mode = 0; alarmWriteBlock(curAlarm, alarm); break; //записать блок основных данных будильника
          case 2: cur_mode = 3; break; //перейти к настройке минут
          case 3: cur_mode = 2; break; //перейти к настройке часов
          case 4: cur_mode = 5; break; //перейти к активации дня недели
          case 5:
          case 6: cur_mode = (alarm[2] < 4) ? 6 : 4; break; //перейти к выбору дня недели
            break;
        }
        blink_data = 0; //сбрасываем флаги
        _timer_ms[TMR_MS] = time_out = 0; //сбрасываем таймеры
        break;
      case SET_KEY_HOLD: //удержание средней кнопки
        switch (cur_mode) {
          case 0: checkAlarms(); return; //выход
          case 1: alarmWriteBlock(curAlarm, alarm); checkAlarms(); return; //записать блок основных данных будильника и выйти
          default:
            MELODY_RESET; //сброс воспроизведения мелодии
            dotSetBright(0); //выключаем точки
            cur_mode = 1; //выбор будильника
            blink_data = 0; //сбрасываем флаги
            _timer_ms[TMR_MS] = time_out = 0; //сбрасываем таймеры
            break;
        }
        break;
    }
  }
}
//-----------------------------Настроки основные------------------------------------
void settings_main(void) //настроки основные
{
  uint8_t cur_mode = 0; //текущий режим
  boolean cur_indi = 0; //текущий индикатор
  boolean set = 0; //режим настройки
  uint8_t time_out = 0; //таймаут автовыхода
  boolean blink_data = 0; //мигание сигментами
  int8_t aging = 0; //буфер регистра старения

  indiClr(); //очищаем индикаторы
  dotSetBright(0); //выключаем точки

  //настройки
  while (1) {
    dataUpdate(); //обработка данных

    if (!_sec) {
      _sec = 1;
      if (++time_out >= SETTINGS_TIMEOUT) return;
    }

    if (!_timer_ms[TMR_MS]) { //если прошло пол секунды
      _timer_ms[TMR_MS] = SETTINGS_BLINK_TIME; //устанавливаем таймер

      indiClr(); //очистка индикаторов
      switch (set) {
        case 0:
          indiPrintNum(cur_mode + 1, (LAMP_NUM / 2 - 1), 2, 0); //вывод режима
          break;
        case 1:
          indiPrintNum(cur_mode + 1, 5); //режим
          switch (cur_mode) {
            case 0: if (!blink_data) indiPrintNum((mainSettings.timeFormat) ? 12 : 24, 2); break;
            case 1: if (!blink_data) indiPrintNum(mainSettings.glitchMode, 3); break;
            case 2: if (!blink_data) indiPrintNum(mainSettings.knock_sound, 3); break;
            case 3:
              if (!blink_data || cur_indi) indiPrintNum(mainSettings.timeHour[0], 0, 2, 0); //вывод часов
              if (!blink_data || !cur_indi) indiPrintNum(mainSettings.timeHour[1], 2, 2, 0); //вывод часов
              break;
            case 4:
              if (!blink_data || cur_indi) indiPrintNum(mainSettings.timeBright[0], 0, 2, 0); //вывод часов
              if (!blink_data || !cur_indi) indiPrintNum(mainSettings.timeBright[1], 2, 2, 0); //вывод часов
              break;
            case 5:
              if (!blink_data || cur_indi) indiPrintNum(mainSettings.indiBright[0], 0, 2, 0); //вывод часов
              if (!blink_data || !cur_indi) indiPrintNum(mainSettings.indiBright[1], 2, 2, 0); //вывод часов
              break;
            case 6:
              if (!blink_data || cur_indi) indiPrintNum(tempSens.temp / 10 + mainSettings.tempCorrect, 0, 3); //вывод часов
              if (!blink_data || !cur_indi) indiPrintNum(mainSettings.sensorSet, 3); //вывод часов
              break;
            case 7:
              if (!blink_data) indiPrintNum(mainSettings.autoTempTime, 1, 3);
              break;
            case 8:
              if (!blink_data) {
                if (EIMSK) indiPrintNum((aging < 0) ? (uint8_t)((aging ^ 0xFF) + 1) : (uint8_t)aging, 0, (aging > 0) ? 4 : 0);
                else indiPrintNum(mainSettings.timePeriod, 0);
              }
              break;
          }
          blink_data = !blink_data; //мигание сигментами
          break;
      }
    }
    //+++++++++++++++++++++  опрос кнопок  +++++++++++++++++++++++++++
    switch (check_keys()) {
      case LEFT_KEY_PRESS: //клик левой кнопкой
        switch (set) {
          case 0: if (cur_mode > 0) cur_mode--; else cur_mode = 8; break;
          case 1:
            switch (cur_mode) {
              case 0: mainSettings.timeFormat = 0; break; //формат времени
              case 1: mainSettings.glitchMode = 0; break; //глюки
              case 2: mainSettings.knock_sound = 0; break; //звук кнопок
              case 3: //время звука смены часа
                switch (cur_indi) {
                  case 0: if (mainSettings.timeHour[0] > 0) mainSettings.timeHour[0]--; else mainSettings.timeHour[0] = 23; break;
                  case 1: if (mainSettings.timeHour[1] > 0) mainSettings.timeHour[1]--; else mainSettings.timeHour[1] = 23; break;
                }
                break;
              case 4: //время смены подсветки
                switch (cur_indi) {
                  case 0: if (mainSettings.timeBright[0] > 0) mainSettings.timeBright[0]--; else mainSettings.timeBright[0] = 23; break;
                  case 1: if (mainSettings.timeBright[1] > 0) mainSettings.timeBright[1]--; else mainSettings.timeBright[1] = 23; break;
                }
                break;
              case 5: //яркость индикаторов
                switch (cur_indi) {
                  case 0: if (mainSettings.indiBright[0] > 0) mainSettings.indiBright[0]--; else mainSettings.indiBright[0] = 30; break;
                  case 1: if (mainSettings.indiBright[1] > 0) mainSettings.indiBright[1]--; else mainSettings.indiBright[1] = 30; break;
                }
                indiSetBright(mainSettings.indiBright[cur_indi]); //установка общей яркости индикаторов
                break;
              case 6: //настройка сенсора температуры
                switch (cur_indi) {
                  case 0: if (mainSettings.tempCorrect > -127) mainSettings.tempCorrect--; else mainSettings.tempCorrect = 127; break;
                  case 1:
                    if (mainSettings.sensorSet > 0) mainSettings.sensorSet--;
                    updateTemp(); //обновить показания температуры
                    break;
                }
                break;
              case 7: //автопоказ температуры
                if (mainSettings.autoTempTime > 5) mainSettings.autoTempTime -= 5; else mainSettings.autoTempTime = 0;
                break;
              case 8: //коррекция хода
                switch (EIMSK) {
                  case 0: if (mainSettings.timePeriod > US_PERIOD_MIN) mainSettings.timePeriod--; else mainSettings.timePeriod = US_PERIOD_MAX; break;
                  case 1: if (aging > -127) aging--; else aging = 127; break;
                }
                break;
            }
            break;
        }
        _timer_ms[TMR_MS] = time_out = blink_data = 0; //сбрасываем флаги
        break;

      case RIGHT_KEY_PRESS: //клик правой кнопкой
        switch (set) {
          case 0: if (cur_mode < 8) cur_mode++; else cur_mode = 0; break;
          case 1:
            switch (cur_mode) {
              case 0: mainSettings.timeFormat = 1; break; //формат времени
              case 1: mainSettings.glitchMode = 1; break; //глюки
              case 2: mainSettings.knock_sound = 1; break; //звук кнопок
              case 3: //время звука смены часа
                switch (cur_indi) {
                  case 0: if (mainSettings.timeHour[0] < 23) mainSettings.timeHour[0]++; else mainSettings.timeHour[0] = 0; break;
                  case 1: if (mainSettings.timeHour[1] < 23) mainSettings.timeHour[1]++; else mainSettings.timeHour[1] = 0; break;
                }
                break;
              case 4: //время смены подсветки
                switch (cur_indi) {
                  case 0: if (mainSettings.timeBright[0] < 23) mainSettings.timeBright[0]++; else mainSettings.timeBright[0] = 0; break;
                  case 1: if (mainSettings.timeBright[1] < 23) mainSettings.timeBright[1]++; else mainSettings.timeBright[1] = 0; break;
                }
                break;
              case 5: //яркость индикаторов
                switch (cur_indi) {
                  case 0: if (mainSettings.indiBright[0] < 30) mainSettings.indiBright[0]++; else mainSettings.indiBright[0] = 0; break;
                  case 1: if (mainSettings.indiBright[1] < 30) mainSettings.indiBright[1]++; else mainSettings.indiBright[1] = 0; break;
                }
                indiSetBright(mainSettings.indiBright[cur_indi]); //установка общей яркости индикаторов
                break;
              case 6: //настройка сенсора температуры
                switch (cur_indi) {
                  case 0: if (mainSettings.tempCorrect < 127) mainSettings.tempCorrect++; else mainSettings.tempCorrect = -127; break;
                  case 1:
                    if (mainSettings.sensorSet < 4) mainSettings.sensorSet++;
                    updateTemp(); //обновить показания температуры
                    break;
                }
                break;
              case 7: //автопоказ температуры
                if (mainSettings.autoTempTime < 240) mainSettings.autoTempTime += 5; else mainSettings.autoTempTime = 240;
                break;
              case 8: //коррекция хода
                switch (EIMSK) {
                  case 0: if (mainSettings.timePeriod < US_PERIOD_MAX) mainSettings.timePeriod++; else mainSettings.timePeriod = US_PERIOD_MIN; break;
                  case 1: if (aging < 127) aging++; else aging = -127; break;
                }
                break;
            }
            break;
        }
        _timer_ms[TMR_MS] = time_out = blink_data = 0; //сбрасываем флаги
        break;

      case LEFT_KEY_HOLD: //удержание левой кнопки
        if (set) {
          cur_indi = 0;
          if (cur_mode == 5) indiSetBright(mainSettings.indiBright[0]); //установка общей яркости индикаторов
        }
        _timer_ms[TMR_MS] = time_out = blink_data = 0; //сбрасываем флаги
        break;

      case RIGHT_KEY_HOLD: //удержание правой кнопки
        if (set) {
          cur_indi = 1;
          if (cur_mode == 5) indiSetBright(mainSettings.indiBright[1]); //установка общей яркости индикаторов
        }
        _timer_ms[TMR_MS] = time_out = blink_data = 0; //сбрасываем флаги
        break;

      case SET_KEY_PRESS: //клик средней кнопкой
        set = !set;
        if (set) {
          switch (cur_mode) {
            case 5: indiSetBright(mainSettings.indiBright[0]); break; //установка общей яркости индикаторов
            case 6:
              updateTemp(); //обновить показания температуры
              break;
            case 8:
              if (EIMSK) aging = readAgingRTC(); //чтение коррекции хода
              break;
          }
          dotSetBright(dotMaxBright); //включаем точки
        }
        else {
          changeBright(); //установка яркости от времени суток
          dotSetBright(0); //выключаем точки
          if (EIMSK && cur_mode == 8) WriteAgingRTC((uint8_t)aging); //запись коррекции хода
        }
        cur_indi = 0;
        _timer_ms[TMR_MS] = time_out = blink_data = 0; //сбрасываем флаги
        break;

      case SET_KEY_HOLD: //удержание средней кнопки
        updateData((uint8_t*)&mainSettings, sizeof(mainSettings), EEPROM_BLOCK_SETTINGS_MAIN, EEPROM_BLOCK_CRC_MAIN); //записываем основные настройки в память
        changeBright(); //установка яркости от времени суток
        _tmrTemp = 0; //сбрасываем таймер показа температуры
        return;
    }
  }
}
//---------------------Установка яркости от времени суток-----------------------------
void changeBright(void) //установка яркости от времени суток
{
  if ((mainSettings.timeBright[0] > mainSettings.timeBright[1] && (timeRTC.h >= mainSettings.timeBright[0] || timeRTC.h < mainSettings.timeBright[1])) ||
      (mainSettings.timeBright[0] < mainSettings.timeBright[1] && timeRTC.h >= mainSettings.timeBright[0] && timeRTC.h < mainSettings.timeBright[1])) {
    //ночной режим
    dotMaxBright = DOT_BRIGHT_N; //установка максимальной яркости точек
    backlMaxBright = BACKL_BRIGHT_N; //установка максимальной яркости подсветки
    indiMaxBright = mainSettings.indiBright[0]; //установка максимальной яркости индикаторов
    FLIP_2_SPEED = (uint16_t)FLIP_MODE_2_TIME * mainSettings.indiBright[1] / mainSettings.indiBright[0]; //расчёт шага яркости режима 2
  }
  else {
    //дневной режим
    dotMaxBright = DOT_BRIGHT; //установка максимальной яркости точек
    backlMaxBright = BACKL_BRIGHT; //установка максимальной яркости подсветки
    indiMaxBright = mainSettings.indiBright[1]; //установка максимальной яркости индикаторов
    FLIP_2_SPEED = (uint16_t)FLIP_MODE_2_TIME; //расчёт шага яркости режима 2
  }
  switch (fastSettings.dotMode) {
    case 0: dotSetBright(0); break; //если точки выключены
    case 1: dotSetBright(dotMaxBright); break; //если точки статичные, устанавливаем яркость
    case 2:
      dotBrightStep = ceil((float)(dotMaxBright * 2.00) / DOT_TIME * DOT_TIMER); //расчёт шага яркости точки
      if (!dotBrightStep) dotBrightStep = 1; //если шаг слишком мал, устанавливаем минимум
      dotBrightTime = ceil(DOT_TIME / (float)(dotMaxBright * 2.00)); //расчёт шага яркости точки
      if (dotBrightTime < DOT_TIMER) dotBrightTime = DOT_TIMER; //если шаг слишком мал, устанавливаем минимум
      break;
  }
#if BACKL_WS2812B
  setLedBright(backlMaxBright); //устанавливаем максимальную яркость
  if (fastSettings.backlMode < 8) setLedColor(fastSettings.backlMode); //отправляем статичный цвет
  if (!backlMaxBright) setLedColor(0); //выключили подсветку
#else
  switch (fastSettings.backlMode) {
    case 0: backlSetBright(0); break; //если посветка выключена
    case 1: backlSetBright(backlMaxBright); break; //если посветка статичная, устанавливаем яркость
    case 2: if (!backlMaxBright) backlSetBright(0); break; //иначе посветка выключена
  }
#endif
  if (backlMaxBright) backlBrightTime = (float)BACKL_STEP / backlMaxBright / 2 * BACKL_TIME; //если подсветка динамичная, расчёт шага дыхания подсветки
  indiSetBright(indiMaxBright); //установка общей яркости индикаторов
}
//----------------------------------Анимация подсветки---------------------------------
void backlEffect(void) //анимация подсветки
{
  if (backlMaxBright && !_timer_ms[TMR_BACKL]) { //если время пришло
    switch (fastSettings.backlMode & 0x7F) {
      case 0: return; //подсветка выключена
      default: { //дыхание подсветки
          static boolean backl_drv; //направление яркости
          static uint8_t backlBright; //яркость
          static uint8_t colorStep; //номер цвета
          if (fastSettings.backlMode > 7) {
            _timer_ms[TMR_BACKL] = backlBrightTime; //установили таймер
            switch (backl_drv) {
              case 0: if (backlBright < backlMaxBright) backlBright += BACKL_STEP; else backl_drv = 1; break;
              case 1:
                if (backlBright > BACKL_MIN_BRIGHT) backlBright -= BACKL_STEP;
                else {
                  backl_drv = 0;
                  if (colorStep < 6) colorStep++; else colorStep = 0;
                  _timer_ms[TMR_BACKL] = BACKL_PAUSE; //установили таймер
                  return; //выходим
                }
                break;
            }
            setLedBright(backlBright); //установили яркость
            setLedColor(((fastSettings.backlMode & 0x7F) == 8) ? (colorStep + 1) : (fastSettings.backlMode & 0x7F)); //отправили цвет
          }
        }
        break;
      case 9: { //плавная смена
          static uint8_t colorStep; //номер цвета
          setLedHue(colorStep++); //установили цвет
          _timer_ms[TMR_BACKL] = BACKL_MODE_9_TIME; //установили таймер
        }
        break;
      case 10: { //радуга
          static uint8_t colorStep; //номер цвета
          colorStep += BACKL_MODE_10_STEP; //прибавили шаг
          for (uint8_t f = 0; f < LAMP_NUM; f++) setLedHue(f, colorStep + (f * BACKL_MODE_10_STEP)); //установили цвет
          _timer_ms[TMR_BACKL] = BACKL_MODE_10_TIME; //установили таймер
        }
        break;
      case 11: { //рандомный цвет
          setLedHue(random(0, LAMP_NUM), random(0, 256)); //установили цвет
          _timer_ms[TMR_BACKL] = BACKL_MODE_11_TIME; //установили таймер
        }
        break;
    }
  }
}
//----------------------------------Мигание подсветки---------------------------------
void backlFlash(void) //мигание подсветки
{
  static boolean backl_drv; //направление яркости
  if (fastSettings.backlMode == 2 && backlMaxBright) {
    if (!_timer_ms[TMR_BACKL]) {
      _timer_ms[TMR_BACKL] = backlBrightTime;
      switch (backl_drv) {
        case 0: if (OCR2A < backlMaxBright) backlSetBright(OCR2A + BACKL_STEP); else backl_drv = 1; break;
        case 1:
          if (OCR2A > BACKL_MIN_BRIGHT) backlSetBright(OCR2A - BACKL_STEP);
          else {
            backl_drv = 0;
            _timer_ms[TMR_BACKL] = BACKL_PAUSE;
          }
          break;
      }
    }
  }
}
//--------------------------------Режим мигания точек------------------------------------
void dotFlashMode(uint8_t mode) //режим мигания точек
{
  static boolean dot_drv; //направление яркости
  static uint8_t dot_cnt; //счетчик мигания точки
  switch (mode) {
    case 0: if (OCR1B) dotSetBright(0); break; //если точки включены, выключаем их
    case 1: if (OCR1B != dotMaxBright) dotSetBright(dotMaxBright); break; //если яркость не совпадает, устанавливаем яркость
    case 2:
      if (!_dot && !_timer_ms[TMR_DOT]) {
        _timer_ms[TMR_DOT] = dotBrightTime;
        switch (dot_drv) {
          case 0: if (OCR1B < dotMaxBright) OCR1B += dotBrightStep; else dot_drv = 1; break;
          case 1:
            if (OCR1B > dotBrightStep) OCR1B -= dotBrightStep;
            else {
              _dot = 1;
              OCR1B = 0;
              dot_drv = 0;
            }
            break;
        }
        dotSetBright(OCR1B); //установка яркости точек
      }
      break;
    case 3:
      if (!_dot && !_timer_ms[TMR_DOT]) {
        switch (dot_drv) {
          case 0: dotSetBright(dotMaxBright); dot_drv = 1; _timer_ms[TMR_DOT] = 500; break; //включаем точки
          case 1: dotSetBright(0); dot_drv = 0; _dot = 1; break; //выключаем точки
        }
      }
      break;
    case 4:
      if (!_dot && !_timer_ms[TMR_DOT]) {
        _timer_ms[TMR_DOT] = 150;
        switch (dot_cnt % 2) {
          case 0: dotSetBright(dotMaxBright); break; //включаем точки
          case 1: dotSetBright(0); break; //выключаем точки
        }
        if (++dot_cnt > 3) {
          _dot = 1;
          dot_cnt = 0;
        }
      }
      break;
  }
}
//--------------------------------Мигание точек------------------------------------
void dotFlash(void) //мигание точек
{
  if (alarmWaint) dotFlashMode((ALM_WAINT_BLINK_DOT != 2) ? ALM_WAINT_BLINK_DOT : fastSettings.dotMode); //мигание точек в режиме ожидания будильника
  else if (alarmEnabled) dotFlashMode((ALM_ON_BLINK_DOT != 2) ? ALM_ON_BLINK_DOT : fastSettings.dotMode); //мигание точек при включенном будильнике
  else dotFlashMode(fastSettings.dotMode); //мигание точек по умолчанию
}
//--------------------------------Обновить показания температуры----------------------------------------
void updateTemp(void) //обновить показания температуры
{
  switch (mainSettings.sensorSet) { //выбор датчика температуры
    case 0: readTempRTC(); break; //чтение температуры с датчика DS3231
    case 1: readTempBME(); break; //чтение температуры/давления/влажности с датчика BME
    case 2: readTempDHT11(); break; //чтение температуры/влажности с датчика DHT11
    case 3: readTempDHT22(); break; //чтение температуры/влажности с датчика DHT22
    case 4: readTempDS(); break; //чтение температуры с датчика DS18B20
  }
}
//--------------------------------Автоматический показ температуры----------------------------------------
void autoShowTemp(void) //автоматический показ температуры
{
  if (mainSettings.autoTempTime && _tmrTemp++ >= mainSettings.autoTempTime && timeRTC.s > 7 && timeRTC.s < 55) {
    _tmrTemp = 0; //сбрасываем таймер

    uint8_t pos = LAMP_NUM; //текущее положение анимации
    boolean drv = 0; //направление анимации

    dotSetBright(dotMaxBright); //включаем точки
    updateTemp(); //обновить показания температуры

    for (uint8_t mode = 0; mode < 3; mode++) {
      switch (mode) {
        case 1:
          if (!tempSens.hum) {
            if (!tempSens.press) return; //выходим
            else mode = 2; //отображаем давление
          }
          dotSetBright(0); //выключаем точки
          break;
        case 2: if (!tempSens.press) return; break; //выходим
      }

      while (1) { //анимация перехода
        dataUpdate(); //обработка данных
        if (check_keys()) return; //возврат если нажата кнопка
        if (!_timer_ms[TMR_ANIM]) { //если таймер истек
          _timer_ms[TMR_ANIM] = AUTO_TEMP_ANIM_TIME; //устанавливаем таймер

          indiClr(); //очистка индикаторов
          switch (mode) {
            case 0: indiPrintNum(tempSens.temp / 10 + mainSettings.tempCorrect, pos, 3, ' '); break; //вывод температуры
            case 1: indiPrintNum(tempSens.hum, pos, 4, ' '); break; //вывод влажности
            case 2: indiPrintNum(tempSens.press, pos, 4, ' '); break; //вывод давления
          }
          if (!drv) {
            if (pos > 0) pos--;
            else {
              drv = 1;
              _timer_ms[TMR_ANIM] = AUTO_TEMP_PAUSE_TIME; //устанавливаем таймер
            }
          }
          else {
            if (pos < LAMP_NUM) pos++;
            else {
              drv = 0;
              break;
            }
          }
        }
      }
    }
  }
}
//--------------------------------Показать температуру----------------------------------------
void showTemp(void) //показать температуру
{
  uint8_t mode = 0; //текущий режим

  _sec = 0; //обновление экрана
  dotSetBright(dotMaxBright); //включаем точки

  updateTemp(); //обновить показания температуры

  for (_timer_ms[TMR_MS] = SHOW_TIME; _timer_ms[TMR_MS];) {
    dataUpdate(); //обработка данных

    if (!_sec) {
      _sec = 1; //сбрасываем флаг
      indiClr(); //очистка индикаторов
      indiPrintNum(mode + 1, 5); //режим
      switch (mode) {
        case 0: indiPrintNum(tempSens.temp / 10 + mainSettings.tempCorrect, 0, 3, ' '); break;
        case 1: indiPrintNum(tempSens.hum, 0, 4, ' '); break;
        case 2: indiPrintNum(tempSens.press, 0, 4, ' '); break;
      }
    }

    switch (check_keys()) {
      case LEFT_KEY_PRESS: //клик левой кнопкой
        if (++mode > 2) mode = 0;
        switch (mode) {
          case 1:
            if (!tempSens.hum) {
              if (!tempSens.press) mode = 0;
              else {
                mode = 2;
                dotSetBright(0); //выключаем точки
              }
            }
            else dotSetBright(0); break; //выключаем точки
          case 2: if (!tempSens.press) mode = 0; break;
        }
        if (!mode) dotSetBright(dotMaxBright); //включаем точки
        _timer_ms[TMR_MS] = SHOW_TIME;
        _sec = 0; //обновление экрана
        break;

      case RIGHT_KEY_PRESS: //клик правой кнопкой
      case SET_KEY_PRESS: //клик средней кнопкой
        return; //выходим
    }
  }
}
//----------------------------------Показать дату-----------------------------------
void showDate(void) //показать дату
{
  uint8_t mode = 0; //текущий режим

  _sec = 0; //обновление экрана
  dotSetBright(dotMaxBright); //включаем точки

  for (_timer_ms[TMR_MS] = SHOW_TIME; _timer_ms[TMR_MS];) {
    dataUpdate(); //обработка данных

    if (!_sec) {
      _sec = 1; //сбрасываем флаг
      indiClr(); //очистка индикаторов
      indiPrintNum(mode + 1, 5); //режим
      switch (mode) {
        case 0:
          indiPrintNum(timeRTC.DD, 0, 2, 0); //вывод даты
          indiPrintNum(timeRTC.MM, 2, 2, 0); //вывод месяца
          break;
        case 1: indiPrintNum(timeRTC.YY, 0); break; //вывод года
      }
    }

    switch (check_keys()) {
      case RIGHT_KEY_PRESS: //клик правой кнопкой
        if (++mode > 1) mode = 0;
        switch (mode) {
          case 0: dotSetBright(dotMaxBright); break; //включаем точки
          case 1: dotSetBright(0); break; //выключаем точки
        }
        _timer_ms[TMR_MS] = SHOW_TIME;
        _sec = 0; //обновление экрана
        break;

      case LEFT_KEY_PRESS: //клик левой кнопкой
      case SET_KEY_PRESS: //клик средней кнопкой
        return; //выходим
    }
  }
}
//----------------------------------Переключение быстрых настроек-----------------------------------
void fastSetSwitch(void) //переключение быстрых настроек
{
  uint8_t anim = 0; //анимация переключения
  uint8_t mode = 0; //режим быстрой настройки

  dotSetBright(0); //выключаем точки

  for (_timer_ms[TMR_MS] = SWITCH_TIME; _timer_ms[TMR_MS];) {
    dataUpdate(); //обработка данных

    if (anim < 4) {
      if (!_timer_ms[TMR_ANIM]) { //если таймер истек
        _timer_ms[TMR_ANIM] = ANIM_TIME; //устанавливаем таймер

        indiClr(); //очистка индикаторов
        indiPrintNum(mode + 1, 5); //режим
        switch (mode) {
          case 0: indiPrintNum(fastSettings.backlMode & 0x7F, anim - 1, 2); break; //вывод режима подсветки
          case 1: indiPrintNum(fastSettings.flipMode, anim); break; //вывод режима анимации
          case 2: indiPrintNum(fastSettings.dotMode, anim); break; //вывод режима точек
        }
        anim++; //сдвигаем анимацию
      }
    }

    switch (check_keys()) {
      case SET_KEY_PRESS: //клик средней кнопкой
        if (mode != 0) mode = 0; //демострация текущего режима работы
        else {
#if BACKL_WS2812B
          setLedBright(backlMaxBright); //устанавливаем максимальную яркость
          if ((fastSettings.backlMode & 0x7F) < 11) fastSettings.backlMode++; else fastSettings.backlMode &= 0x80; //переключили режим подсветки
          if ((fastSettings.backlMode & 0x7F) < 8) setLedColor((fastSettings.backlMode & 0x7F)); //отправляем статичный цвет
#else
          if (++fastSettings.backlMode > 2) fastSettings.backlMode = 0; //переключили режим подсветки
          switch (fastSettings.backlMode) {
            case 0: backlSetBright(0); break; //выключаем подсветку
            case 1: backlSetBright(backlMaxBright); break; //включаем подсветку
            case 2: backlSetBright(backlMaxBright ? BACKL_MIN_BRIGHT : 0); break; //выключаем подсветку
          }
#endif
        }
        _timer_ms[TMR_MS] = SWITCH_TIME;
        anim = 0;
        break;
#if BACKL_WS2812B
      case SET_KEY_HOLD: //удержание средней кнопки
        if (mode != 0) mode = 0; //демострация текущего режима работы
        else {
          if (fastSettings.backlMode & 0x80) { //если режим дыхания включен
            fastSettings.backlMode &= 0x7F; //выключили режим дыхания
            if (fastSettings.backlMode < 8) { //если выбран статичный режим
              setLedBright(backlMaxBright); //устанавливаем максимальную яркость
              setLedColor(fastSettings.backlMode); //отправляем статичный цвет
            }
          }
          else fastSettings.backlMode |= 0x80; //иначе включили режим дыхания
        }
        _timer_ms[TMR_MS] = SWITCH_TIME;
        break;
#endif
      case RIGHT_KEY_PRESS: //клик правой кнопкой
        if (mode != 1) mode = 1; //демострация текущего режима работы
        else if (++fastSettings.flipMode > FLIP_EFFECT_NUM) fastSettings.flipMode = 0;
        _timer_ms[TMR_MS] = SWITCH_TIME;
        anim = 0;
        break;

      case LEFT_KEY_PRESS: //клик левой кнопкой
        if (mode != 2) mode = 2; //демострация текущего режима работы
        else if (++fastSettings.dotMode > 2) fastSettings.dotMode = 0;
        _timer_ms[TMR_MS] = SWITCH_TIME;
        anim = 0;
        break;
    }
  }
  if (mode == 1) flipIndi(fastSettings.flipMode, 1); //демонстрация анимации цифр
  updateData((uint8_t*)&fastSettings, sizeof(fastSettings), EEPROM_BLOCK_SETTINGS_FAST, EEPROM_BLOCK_CRC_FAST); //записываем настройки яркости в память
}
//--------------------------------Тревога таймера----------------------------------------
void timerWarn(void) //тревога таймера
{
  boolean blink_data = 0; //флаг мигания индикаторами
  if (timerMode == 2 && !timerCnt) {
    while (!check_keys()) { //ждем
      dataUpdate(); //обработка данных
      MELODY_PLAY(0); //воспроизводим мелодию
      if (!_timer_ms[TMR_ANIM]) {
        _timer_ms[TMR_ANIM] = TIMER_BLINK_TIME;
        switch (blink_data) {
          case 0: indiClr(); dotSetBright(0); break; //очищаем индикаторы и выключаем точки
          case 1:
            indiPrintNum((timerTime < 3600) ? ((timerTime / 60) % 60) : (timerTime / 3600), 0, 2, 0); //вывод минут/часов
            indiPrintNum((timerTime < 3600) ? (timerTime % 60) : ((timerTime / 60) % 60), 2, 2, 0); //вывод секунд/минут
            indiPrintNum((timerTime < 3600) ? 0 : (timerTime % 60), 4, 2, 0); //вывод секунд
            dotSetBright(dotMaxBright); //включаем точки
            break;
        }
        blink_data = !blink_data; //мигаем временем
      }
    }
    timerMode = 0; //деактивируем таймер
    timerCnt = timerTime; //сбрасываем таймер
  }
}
//----------------------------Настройки таймера----------------------------------
void timerSettings(void) //настройки таймера
{
  boolean mode = 0; //текущий режим
  boolean blink_data = 0; //флаг мигания индикаторами

  dotSetBright(0); //выключаем точки
  while (1) {
    dataUpdate(); //обработка данных

    if (!_timer_ms[TMR_MS]) { //если прошло пол секунды
      _timer_ms[TMR_MS] = SETTINGS_BLINK_TIME; //устанавливаем таймер

      indiClr(); //очистка индикаторов
      indiPrintNum(2, 5); //вывод режима

      if (!blink_data || mode) indiPrintNum(timerTime / 60, 0, 2, 0); //вывод минут
      if (!blink_data || !mode) indiPrintNum(timerTime % 60, 2, 2, 0); //вывод секунд
      blink_data = !blink_data;
    }

    switch (check_keys()) {
      case SET_KEY_PRESS: //клик средней кнопкой
        mode = !mode; //переключаем режим
        _timer_ms[TMR_MS] = blink_data = 0; //сбрасываем флаги
        break;

      case RIGHT_KEY_PRESS: //клик правой кнопкой
        switch (mode) {
          case 0: if (timerTime / 60 < 99) timerTime += 60; else timerTime -= 5940; break; //сбрасываем секундомер
          case 1: if (timerTime % 60 < 59) timerTime++; else timerTime -= 59; break; //сбрасываем таймер
        }
        _timer_ms[TMR_MS] = blink_data = 0; //сбрасываем флаги
        break;

      case LEFT_KEY_PRESS: //клик левой кнопкой
        switch (mode) {
          case 0: if (timerTime / 60 > 0) timerTime -= 60; else timerTime += 5940; break; //сбрасываем секундомер
          case 1: if (timerTime % 60 > 0) timerTime--; else timerTime += 59; break; //сбрасываем таймер
        }
        _timer_ms[TMR_MS] = blink_data = 0; //сбрасываем флаги
        break;

      case ADD_KEY_HOLD: //удержание дополнительной кнопки
      case SET_KEY_HOLD: //удержание средней кнопки
        if (!timerTime) timerTime = TIMER_TIME; //устанавливаем значение по умолчанию
        timerCnt = timerTime; //сбрасываем таймер
        return; //выходим
    }
  }
}
//--------------------------------Таймер-секундомер----------------------------------------
void timerStopwatch(void) //таймер-секундомер
{
  uint8_t mode = 0; //текущий режим
  uint8_t time_out = 0; //таймаут автовыхода
  uint16_t msTmr = 10000UL / mainSettings.timePeriod * 2; //расчет хода миллисекунд
  static uint16_t millisCnt; //счетчик миллисекун

  if (timerMode & 0x7F) mode = (timerMode & 0x7F) - 1; //если таймер был запущен
  else timerCnt = 0; //иначе сбрасываем таймер

  _sec = 0; //обновление экрана

  while (1) {
    dataUpdate(); //обработка данных
    timerWarn(); //тревога таймера
#if LAMP_NUM > 4
    dotFlashMode((!timerCnt) ? 0 : ((timerMode > 2 || !timerMode ) ? 1 : 3)); //мигание точек по умолчанию
#else
    dotFlashMode((!timerMode) ? 0 : ((timerMode > 2) ? 1 : 3)); //мигание точек по умолчанию
#endif

    if (!_sec) {
      _sec = 1; //сбрасываем флаг
      if (!timerMode && ++time_out >= SETTINGS_TIMEOUT) return;

      indiClr(); //очистка индикаторов
      switch (timerMode) {
        case 0: indiPrintNum(mode + 1, 5); break; //вывод режима
        default:
          if (!(timerMode & 0x80)) millisCnt = 0; //сбрасываем счетчик миллисекунд
          indiPrintNum((timerCnt < 3600) ? ((mode) ? (100 - millisCnt / 10) : (millisCnt / 10)) : (timerCnt % 60), 4, 2, 0); //вывод милиекунд/секунд
          break;
      }

      indiPrintNum((timerCnt < 3600) ? ((timerCnt / 60) % 60) : (timerCnt / 3600), 0, 2, 0); //вывод минут/часов
      indiPrintNum((timerCnt < 3600) ? (timerCnt % 60) : ((timerCnt / 60) % 60), 2, 2, 0); //вывод секунд/минут
    }

    switch (timerMode) {
      case 1: case 2:
        if (!_timer_ms[TMR_MS]) {
          _timer_ms[TMR_MS] = 10;
          if (timerCnt < 3600) {
            millisCnt += msTmr;
            indiPrintNum((mode) ? (100 - millisCnt / 10) : (millisCnt / 10), 4, 2, 0); //вывод милиекунд
          }
        }
        break;
    }

    switch (check_keys()) {
      case SET_KEY_PRESS: //клик средней кнопкой
        if (mode && !timerMode) {
          timerSettings(); //настройки таймера
          time_out = 0; //сбрасываем таймер автовыхода
          _sec = 0; //обновление экрана
        }
        break;

      case SET_KEY_HOLD: //удержание средней кнопки
        if (timerMode == 1) timerMode |= 0x80; //приостановка секундомера
        return; //выходим

      case RIGHT_KEY_PRESS: //клик правой кнопкой
      case RIGHT_KEY_HOLD: //удержание правой кнопки
        mode = 1; //переключаем режим
        timerMode = 0; //деактивируем таймер
        timerCnt = timerTime; //сбрасываем таймер
        time_out = 0; //сбрасываем таймер автовыхода
        _sec = 0; //обновление экрана
        break;

      case LEFT_KEY_PRESS: //клик левой кнопкой
      case LEFT_KEY_HOLD: //удержание левой кнопки
        mode = 0; //переключаем режим
        timerMode = 0; //деактивируем таймер
        timerCnt = 0; //сбрасываем секундомер
        time_out = 0; //сбрасываем таймер автовыхода
        _sec = 0; //обновление экрана
        break;

      case ADD_KEY_PRESS: //клик дополнительной кнопкой
        if (!timerMode) {
          millisCnt = 0; //сбрасываем счетчик миллисекунд
          timerMode = mode + 1;
        }
        else timerMode ^= 0x80; //приостановка таймера/секундомера
        time_out = 0; //сбрасываем таймер автовыхода
        _sec = 0; //обновление экрана
        break;

      case ADD_KEY_HOLD: //удержание дополнительной кнопки
        timerMode = 0; //деактивируем таймер
        switch (mode) {
          case 0: timerCnt = 0; break; //сбрасываем секундомер
          case 1: timerCnt = timerTime; break; //сбрасываем таймер
        }
        time_out = 0; //сбрасываем таймер автовыхода
        _sec = 0; //обновление экрана
        break;
    }
  }
}
//----------------------------Антиотравление индикаторов-------------------------------
void burnIndi(void) //антиотравление индикаторов
{
#if BURN_INDI_TYPE
  if (_tmrBurn >= BURN_PERIOD && timeRTC.s >= BURN_PHASE) {
    _tmrBurn = 0; //сбрасываем таймер
    dotSetBright(0); //выключаем точки
    for (byte indi = 0; indi < LAMP_NUM; indi++) {
      indiClr(); //очистка индикаторов
      for (byte loops = 0; loops < BURN_LOOPS; loops++) {
        for (byte digit = 0; digit < 10; digit++) {
          indiPrintNum(cathodeMask[digit], indi); //отрисовываем цифру
          for (_timer_ms[TMR_MS] = BURN_TIME; _timer_ms[TMR_MS];) { //ждем
            if (check_keys()) { //если нажата кнопка
              indiPrintNum((mainSettings.timeFormat) ? get_12h(timeRTC.h) : timeRTC.h, 0, 2, 0); //вывод часов
              indiPrintNum(timeRTC.m, 2, 2, 0); //вывод минут
              return; //выходим
            }
            dataUpdate(); //обработка данных
          }
        }
      }
    }
    indiPrintNum((mainSettings.timeFormat) ? get_12h(timeRTC.h) : timeRTC.h, 0, 2, 0); //вывод часов
    indiPrintNum(timeRTC.m, 2, 2, 0); //вывод минут
  }
#else
  if (_tmrBurn >= BURN_PERIOD && timeRTC.s >= BURN_PHASE) {
    _tmrBurn = 0; //сбрасываем таймер
    for (byte indi = 0; indi < LAMP_NUM; indi++) {
      for (byte loops = 0; loops < BURN_LOOPS; loops++) {
        for (byte digit = 0; digit < 10; digit++) {
          indiPrintNum(cathodeMask[digit], indi); //отрисовываем цифру
          for (_timer_ms[TMR_MS] = BURN_TIME; _timer_ms[TMR_MS];) { //ждем
            if (check_keys()) { //если нажата кнопка
              indiPrintNum((mainSettings.timeFormat) ? get_12h(timeRTC.h) : timeRTC.h, 0, 2, 0); //вывод часов
              indiPrintNum(timeRTC.m, 2, 2, 0); //вывод минут
              return; //выходим
            }
            dataUpdate(); //обработка данных
            dotFlash(); //мигаем точками
          }
        }
      }
      indiPrintNum((mainSettings.timeFormat) ? get_12h(timeRTC.h) : timeRTC.h, 0, 2, 0); //вывод часов
      indiPrintNum(timeRTC.m, 2, 2, 0); //вывод минут
      indiPrintNum(timeRTC.s, 4, 2, 0); //вывод секунд
    }
  }
#endif
}
//----------------------------------Анимация цифр-----------------------------------
void flipIndi(uint8_t flipMode, boolean demo) //анимация цифр
{
  uint8_t mode;
  _animShow = 0; //сбрасываем флаг

  switch (flipMode) {
    case 0: return;
    case 1: if (demo) return; else mode = random(0, FLIP_EFFECT_NUM - 1); break;
    default: mode = flipMode - 2; break;
  }

  boolean flipIndi[4] = {0, 0, 0, 0};
  uint8_t anim_buf[12];
  uint8_t drvIndi = 1;
  uint8_t HH;
  uint8_t MM;

  if (timeRTC.m) {
    MM = timeRTC.m - 1;
    HH = timeRTC.h;
  }
  else {
    MM = 59;
    if (timeRTC.h) HH = timeRTC.h - 1;
    else HH = 23;
  }

  if (!demo) {
    if (timeRTC.h / 10 != HH / 10) flipIndi[0] = 1;
    if (timeRTC.h % 10 != HH % 10) flipIndi[1] = 1;
    if (timeRTC.m / 10 != MM / 10) flipIndi[2] = 1;
    if (timeRTC.m % 10 != MM % 10) flipIndi[3] = 1;
  }
  else {
    indiPrintNum((mainSettings.timeFormat) ? get_12h(timeRTC.h) : timeRTC.h, 0, 2, 0); //вывод часов
    indiPrintNum(timeRTC.m, 2, 2, 0); //вывод минут
    for (uint8_t i = 0; i < 4; i++) flipIndi[i] = 1;
  }

  switch (mode) {
    case 0: //плавное угасание и появление
      anim_buf[0] = indiMaxBright;

      while (!check_keys()) {
        dataUpdate(); //обработка данных
        dotFlash(); //мигаем точками
        indiPrintNum(timeRTC.s, 4, 2, 0); //вывод секунд

        if (!_timer_ms[TMR_ANIM]) { //если таймер истек
          if (drvIndi) {
            if (anim_buf[0] > 0) {
              anim_buf[0]--;
              for (uint8_t i = 0; i < 4; i++) if (flipIndi[i]) indiSetBright(anim_buf[0], i);
            }
            else {
              drvIndi = 0;
              indiPrintNum((mainSettings.timeFormat) ? get_12h(timeRTC.h) : timeRTC.h, 0, 2, 0); //вывод часов
              indiPrintNum(timeRTC.m, 2, 2, 0); //вывод минут
            }
          }
          else {
            if (anim_buf[0] < indiMaxBright) {
              anim_buf[0]++;
              for (uint8_t i = 0; i < 4; i++) if (flipIndi[i]) indiSetBright(anim_buf[0], i);
            }
            else break;
          }
          _timer_ms[TMR_ANIM] = FLIP_MODE_2_TIME; //устанавливаем таймер
        }
      }
      indiSetBright(indiMaxBright); //возвращаем максимальную яркость
      break;
    case 1: //перемотка по порядку числа
      //старое время
      anim_buf[0] = HH / 10;
      anim_buf[1] = HH % 10;
      anim_buf[2] = MM / 10;
      anim_buf[3] = MM % 10;
      //новое время
      anim_buf[4] = timeRTC.h / 10;
      anim_buf[5] = timeRTC.h % 10;
      anim_buf[6] = timeRTC.m / 10;
      anim_buf[7] = timeRTC.m % 10;

      while (!check_keys()) {
        dataUpdate(); //обработка данных
        dotFlash(); //мигаем точками
        indiPrintNum(timeRTC.s, 4, 2, 0); //вывод секунд

        if (!_timer_ms[TMR_ANIM]) { //если таймер истек
          drvIndi = 0;
          for (uint8_t i = 0; i < 4; i++) {
            if (flipIndi[i]) {
              if (anim_buf[i]) anim_buf[i]--;
              else anim_buf[i] = 9;
              if (anim_buf[i] == anim_buf[i + 4]) flipIndi[i] = 0;
              else drvIndi = 1;
              indiPrintNum(anim_buf[i], i);
            }
          }
          if (!drvIndi) break;
          _timer_ms[TMR_ANIM] = FLIP_MODE_3_TIME; //устанавливаем таймер
        }
      }
      break;
    case 2: //перемотка по порядку катодов в лампе
      if (!demo) {
        for (uint8_t c = 0; c < 10; c++) {
          if (cathodeMask[c] == timeRTC.h / 10) anim_buf[0] = c;
          if (cathodeMask[c] == HH / 10) anim_buf[4] = c;
          if (cathodeMask[c] == timeRTC.h % 10) anim_buf[1] = c;
          if (cathodeMask[c] == HH % 10) anim_buf[5] = c;
          if (cathodeMask[c] == timeRTC.m / 10) anim_buf[2] = c;
          if (cathodeMask[c] == MM / 10) anim_buf[6] = c;
          if (cathodeMask[c] == timeRTC.m % 10) anim_buf[3] = c;
          if (cathodeMask[c] == MM % 10) anim_buf[7] = c;
        }
      }
      else {
        for (uint8_t i = 0; i < 4; i++) {
          for (uint8_t c = 0; c < 10; c++) {
            anim_buf[i] = 0;
            anim_buf[i + 4] = 9;
          }
        }
      }

      while (!check_keys()) {
        dataUpdate(); //обработка данных
        dotFlash(); //мигаем точками
        indiPrintNum(timeRTC.s, 4, 2, 0); //вывод секунд

        if (!_timer_ms[TMR_ANIM]) { //если таймер истек
          drvIndi = 0;
          for (uint8_t i = 0; i < 4; i++) {
            if (flipIndi[i]) {
              drvIndi = 1;
              if (anim_buf[i] > anim_buf[i + 4]) anim_buf[i]--;
              else if (anim_buf[i] < anim_buf[i + 4]) anim_buf[i]++;
              else flipIndi[i] = 0;
              indiPrintNum(cathodeMask[anim_buf[i]], i);
            }
          }
          if (!drvIndi) break;
          _timer_ms[TMR_ANIM] = FLIP_MODE_4_TIME; //устанавливаем таймер
        }
      }
      break;
    case 3: //поезд
      //старое время
      anim_buf[0] = HH; //часы
      anim_buf[1] = MM; //минуты
      //новое время
      anim_buf[2] = timeRTC.h; //часы
      anim_buf[3] = timeRTC.m; //минуты

      for (uint8_t c = 0; c < 2; c++) {
        for (uint8_t i = 0; i < 4;) {
          dataUpdate(); //обработка данных
          dotFlash(); //мигаем точками
          indiPrintNum(timeRTC.s, 4, 2, 0); //вывод секунд

          if (check_keys()) return; //возврат если нажата кнопка
          if (!_timer_ms[TMR_ANIM]) { //если таймер истек
            indiClr(); //очистка индикатора
            switch (c) {
              case 0: indiPrintNum(anim_buf[0] * 100 + anim_buf[1], i + 1); break; //вывод часов
              case 1: indiPrintNum(anim_buf[2] * 100 + anim_buf[3], -2 + i); break; //вывод часов
            }
            i++; //прибавляем цикл
            _timer_ms[TMR_ANIM] = FLIP_MODE_5_TIME; //устанавливаем таймер
          }
        }
      }
      break;
    case 4: //резинка
      //старое время
      anim_buf[3] = HH / 10; //часы
      anim_buf[2] = HH % 10; //часы
      anim_buf[1] = MM / 10; //минуты
      anim_buf[0] = MM % 10; //минуты
      //новое время
      anim_buf[4] = timeRTC.h / 10; //часы
      anim_buf[5] = timeRTC.h % 10; //часы
      anim_buf[6] = timeRTC.m / 10; //минуты
      anim_buf[7] = timeRTC.m % 10; //минуты

      drvIndi = 0;

      for (uint8_t c = 0; c < 2; c++) {
        for (uint8_t i = 0; i < 4;) {
          dataUpdate(); //обработка данных
          dotFlash(); //мигаем точками
          indiPrintNum(timeRTC.s, 4, 2, 0); //вывод секунд

          if (check_keys()) return; //возврат если нажата кнопка
          if (!_timer_ms[TMR_ANIM]) { //если таймер истек
            switch (c) {
              case 0:
                for (uint8_t b = i + 1; b > 0; b--) {
                  if (b - 1 == i - drvIndi) indiPrintNum(anim_buf[i], 4 - b); //вывод часов
                  else indiClr(4 - b); //очистка индикатора
                }
                if (drvIndi++ >= i) {
                  drvIndi = 0; //сбрасываем позицию индикатора
                  i++; //прибавляем цикл
                }
                break;
              case 1:
                for (uint8_t b = 0; b < 4 - i; b++) {
                  if (b == drvIndi) indiPrintNum(anim_buf[7 - i], b); //вывод часов
                  else indiClr(b); //очистка индикатора
                }
                if (drvIndi++ >= 3 - i) {
                  drvIndi = 0; //сбрасываем позицию индикатора
                  i++; //прибавляем цикл
                }
                break;
            }
            _timer_ms[TMR_ANIM] = FLIP_MODE_6_TIME; //устанавливаем таймер
          }
        }
      }
      break;
    case 5: //волна
      //новое время
      anim_buf[3] = timeRTC.h / 10; //часы
      anim_buf[2] = timeRTC.h % 10; //часы
      anim_buf[1] = timeRTC.m / 10; //минуты
      anim_buf[0] = timeRTC.m % 10; //минуты

      for (uint8_t c = 0; c < 2; c++) {
        for (uint8_t i = 0; i < 4;) {
          dataUpdate(); //обработка данных
          dotFlash(); //мигаем точками
          indiPrintNum(timeRTC.s, 4, 2, 0); //вывод секунд

          if (check_keys()) return; //возврат если нажата кнопка
          if (!_timer_ms[TMR_ANIM]) { //если таймер истек
            switch (c) {
              case 0: indiClr(3 - i); break; //очистка индикатора
              case 1: indiPrintNum(anim_buf[i], 3 - i); break; //вывод часов
            }
            i++; //прибавляем цикл
            _timer_ms[TMR_ANIM] = FLIP_MODE_7_TIME; //устанавливаем таймер
          }
        }
      }
      break;
    case 6: //блики
      //новое время
      anim_buf[0] = timeRTC.h / 10; //часы
      anim_buf[1] = timeRTC.h % 10; //часы
      anim_buf[2] = timeRTC.m / 10; //минуты
      anim_buf[3] = timeRTC.m % 10; //минуты

      for (uint8_t i = 0; i < 4;) {
        drvIndi = random(0, 4);
        for (uint8_t c = 0; c < 2;) {
          dataUpdate(); //обработка данных
          dotFlash(); //мигаем точками
          indiPrintNum(timeRTC.s, 4, 2, 0); //вывод секунд

          if (check_keys()) return; //возврат если нажата кнопка
          if (!_timer_ms[TMR_ANIM]) { //если таймер истек
            for (uint8_t b = 0; b < i; b++) {
              while (anim_buf[4 + b] == drvIndi) {
                drvIndi = random(0, 4);
                b = 0;
              }
            }
            anim_buf[4 + i] = drvIndi;
            switch (c) {
              case 0: indiClr(drvIndi); break; //очистка индикатора
              case 1:
                indiPrintNum(anim_buf[drvIndi], drvIndi);
                i++; //прибавляем цикл
                break; //вывод часов
            }
            c++; //прибавляем цикл
            _timer_ms[TMR_ANIM] = FLIP_MODE_8_TIME; //устанавливаем таймер
          }
        }
      }
      break;
    case 7: //испарение
      //новое время
      anim_buf[0] = timeRTC.h / 10; //часы
      anim_buf[1] = timeRTC.h % 10; //часы
      anim_buf[2] = timeRTC.m / 10; //минуты
      anim_buf[3] = timeRTC.m % 10; //минуты

      for (uint8_t c = 0; c < 2; c++) {
        drvIndi = random(0, 4);
        for (uint8_t i = 0; i < 4;) {
          dataUpdate(); //обработка данных
          dotFlash(); //мигаем точками
          indiPrintNum(timeRTC.s, 4, 2, 0); //вывод секунд

          if (check_keys()) return; //возврат если нажата кнопка
          if (!_timer_ms[TMR_ANIM]) { //если таймер истек
            for (uint8_t b = 0; b < i; b++) {
              while (anim_buf[4 + b] == drvIndi) {
                drvIndi = random(0, 4);
                b = 0;
              }
            }
            anim_buf[4 + i] = drvIndi;
            switch (c) {
              case 0: indiClr(drvIndi); break; //очистка индикатора
              case 1: indiPrintNum(anim_buf[drvIndi], drvIndi); break; //вывод часов
            }
            i++; //прибавляем цикл
            _timer_ms[TMR_ANIM] = FLIP_MODE_9_TIME; //устанавливаем таймер
          }
        }
      }
      break;
  }
}
//-----------------------------Главный экран------------------------------------------------
void mainScreen(void) //главный экран
{
  if (_animShow) flipIndi(fastSettings.flipMode, 0); //анимация цифр основная

  if (!_sec) {
    _sec = 1; //сбрасываем флаг

    burnIndi(); //антиотравление индикаторов
    glitchMode(); //имитация глюков
    autoShowTemp(); //автоматический показ температуры

    indiPrintNum((mainSettings.timeFormat) ? get_12h(timeRTC.h) : timeRTC.h, 0, 2, 0); //вывод часов
    indiPrintNum(timeRTC.m, 2, 2, 0); //вывод минут
    indiPrintNum(timeRTC.s, 4, 2, 0); //вывод секунд
  }

  switch (check_keys()) {
    case LEFT_KEY_HOLD: //удержание левой кнопки
      settings_time(); //иначе настройки времени
      _sec = _animShow = 0; //обновление экрана
      break;
    case LEFT_KEY_PRESS: //клик левой кнопкой
      showTemp(); //показать температуру
      _sec = _animShow = 0; //обновление экрана
      break;
    case RIGHT_KEY_PRESS: //клик правой кнопкой
      showDate(); //показать дату
      _sec = _animShow = 0; //обновление экрана
      break;
    case RIGHT_KEY_HOLD: //удержание правой кнопки
#if USE_ONE_ALARM
      settings_singleAlarm(); //настройка будильника
#else
      settings_multiAlarm(); //настройка будильников
#endif
      _sec = _animShow = 0; //обновление экрана
      break;
    case SET_KEY_PRESS: //клик средней кнопкой
      fastSetSwitch(); //переключение настроек
      _sec = _animShow = 0; //обновление экрана
      break;
    case SET_KEY_HOLD: //удержание средней кнопки
      if (alarmWaint) {
        buzz_pulse(ALM_OFF_SOUND_FREQ, ALM_OFF_SOUND_TIME); //звук выключения будильника
        alarmReset(); //сброс будильника
      }
      else settings_main(); //настроки основные
      _sec = _animShow = 0; //обновление экрана
      break;
    case ADD_KEY_PRESS: //клик дополнительной кнопкой
      timerStopwatch(); //таймер-секундомер
      _sec = _animShow = 0; //обновление экрана
      break;
  }
}
//-------------------------------Получить 12-ти часовой формат---------------------------------------------------
uint8_t get_12h(uint8_t timeH) //получить 12-ти часовой формат
{
  return (timeH > 12) ? (timeH - 12) : (timeH) ? timeH : 12;
}
