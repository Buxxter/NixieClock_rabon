#if LAMP_REVERS
#if LAMP_NUM < 6
volatile uint8_t* anodePort[] = {&ANODE_4_PORT, &ANODE_3_PORT, &ANODE_2_PORT, &ANODE_1_PORT}; //таблица портов анодов ламп
const uint8_t anodeBit[] = {0x01 << ANODE_4_BIT, 0x01 << ANODE_3_BIT, 0x01 << ANODE_2_BIT, 0x01 << ANODE_1_BIT}; //таблица бит анодов ламп
#else
volatile uint8_t* anodePort[] = {&ANODE_6_PORT, &ANODE_5_PORT, &ANODE_4_PORT, &ANODE_3_PORT, &ANODE_2_PORT, &ANODE_1_PORT}; //таблица портов анодов ламп
const uint8_t anodeBit[] = {0x01 << ANODE_6_BIT, 0x01 << ANODE_5_BIT, 0x01 << ANODE_4_BIT, 0x01 << ANODE_3_BIT, 0x01 << ANODE_2_BIT, 0x01 << ANODE_1_BIT}; //таблица бит анодов ламп
#endif
#else
volatile uint8_t* anodePort[] = {&ANODE_1_PORT, &ANODE_2_PORT, &ANODE_3_PORT, &ANODE_4_PORT, &ANODE_5_PORT, &ANODE_6_PORT}; //таблица портов анодов ламп
const uint8_t anodeBit[] = {0x01 << ANODE_1_BIT, 0x01 << ANODE_2_BIT, 0x01 << ANODE_3_BIT, 0x01 << ANODE_4_BIT, 0x01 << ANODE_5_BIT, 0x01 << ANODE_6_BIT}; //таблица бит анодов ламп
#endif

const uint8_t decoderBit[] = {3, 1, 0, 2}; //порядок битов дешефратора(3, 1, 0, 2)
const uint8_t decoderMask[] = {DECODER_1, DECODER_2, DECODER_3, DECODER_4}; //порядок и номера пинов дешефратора(0, 1, 2, 3)

uint8_t indi_buf[LAMP_NUM]; //буфер индикаторов
uint8_t indi_dimm[LAMP_NUM]; //яркость индикаторов
uint8_t indi_null; //пустой сивол(отключеный индикатор)
volatile uint8_t indiState; //текущей номер отрисовки индикатора

volatile uint8_t tick_ms; //счетчик тиков миллисекунд
volatile uint8_t tick_sec; //счетчик тиков от RTC

#if NEON_DOT
#define _INDI_ON  TCNT0 = 255; TIMSK0 |= (0x01 << OCIE0B | 0x01 << OCIE0A | 0x01 << TOIE0)
#define _INDI_OFF TIMSK0 &= ~(0x01 << OCIE0B | 0x01 << OCIE0A | 0x01 << TOIE0); indiState = 0
#else
#define _INDI_ON  TCNT0 = 255; TIMSK0 |= (0x01 << OCIE0A | 0x01 << TOIE0)
#define _INDI_OFF TIMSK0 &= ~(0x01 << OCIE0A | 0x01 << TOIE0); indiState = 0
#endif

void indiPrintNum(uint16_t num, int8_t indi, uint8_t length = 0, char filler = ' ');

//---------------------------------Динамическая индикация---------------------------------------
ISR(TIMER0_OVF_vect) //динамическая индикация
{
  OCR0A = indi_dimm[indiState]; //устанавливаем яркость индикатора
#if NEON_DOT
  OCR0B = OCR1B;
#endif

  PORTC = (PORTC & 0xF0) | indi_buf[indiState]; //отправляем в дешефратор буфер индикатора
  *anodePort[indiState] |= (indi_buf[indiState] != indi_null) ? anodeBit[indiState] : 0x00; //включаем индикатор если не пустой символ

#if NEON_DOT
  if (OCR0B) DOT_ON;
#endif

  tick_ms++; //прибавляем тик
}
ISR(TIMER0_COMPA_vect) {
  *anodePort[indiState] &= ~anodeBit[indiState]; //выключаем индикатор
  if (++indiState > (LAMP_NUM - 1)) indiState = 0; //переходим к следующему индикатору
}
#if NEON_DOT
ISR(TIMER0_COMPB_vect, ISR_NAKED) {
  DOT_OFF;
  reti();
}
#endif
//-------------------------Инициализация индикаторов----------------------------------------------------
void IndiInit(void) //инициализация индикаторов
{
  for (uint8_t dec = 0; dec < 4; dec++) {
    if ((0x0A >> dec) & 0x01) indi_null |= (0x01 << decoderBit[dec]); //находим пустой символ
  }
  for (uint8_t i = 0; i < 4; i++) { //инициализируем пины
    PORTC |= (0x01 << decoderMask[i]); //устанавливаем высокий уровень катода
    DDRC |= (0x01 << decoderMask[i]); //устанавливаем катод как выход
  }
  for (uint8_t i = 0; i < LAMP_NUM; i++) { //инициализируем пины
    *anodePort[i] &= ~anodeBit[i]; //устанавливаем низкий уровень анода
    *(anodePort[i] - 1) |= anodeBit[i]; //устанавливаем анод как выход

    indi_dimm[i] = 120; //устанавливаем максимальную юркость
    indi_buf[i] = indi_null; //очищаем буфер пустыми символами
  }

  OCR0A = 120; //максимальная яркость
  OCR0B = 0; //выключаем точки

  TIMSK0 = 0; //отключаем прерывания Таймера0
  TCCR0A = 0; //отключаем OC0A/OC0B
  TCCR0B = (1 << CS02); //пределитель 256

  OCR1A = MIN_PWM; //устанавливаем первичное значение шим
  OCR1B = 0; //выключаем точки

  TIMSK1 = 0; //отключаем прерывания Таймера1
#if NEON_DOT
  TCCR1A = (1 << COM1A1 | 1 << WGM10); //подключаем D9
#else
  TCCR1A = (1 << COM1B1 | 1 << COM1A1 | 1 << WGM10); //подключаем D9 и D10
#endif
  TCCR1B = (1 << CS10);  //задаем частоту ШИМ на 9 и 10 выводах 31 кГц

  OCR2A = 0; //выключаем подсветку

  TIMSK2 = 0; //отключаем прерывания Таймера2
#if BACKL_WS2812B
  TCCR2A = (1 << WGM20 | 1 << WGM21); //отключаем OCR2A и OCR2B
#else
  TCCR2A = (1 << COM2A1 | 1 << WGM20 | 1 << WGM21); //подключаем D11
#endif
  TCCR2B = (1 << CS21); //пределитель 8

  sei(); //разрешаем прерывания глобально

  _INDI_ON;
}
//---------------------------------Установка Linear Advance---------------------------------------
void indiChangePwm(void) //установка Linear Advance
{
  uint16_t dimm_all = 0;
  for (uint8_t i = 0; i < LAMP_NUM; i++) if (indi_buf[i] != indi_null) dimm_all += indi_dimm[i];
  OCR1A = MIN_PWM + (float)(dimm_all / LAMP_NUM) * ((float)(MAX_PWM - MIN_PWM) / 120.0);
}
//-------------------------Очистка индикаторов----------------------------------------------------
void indiClr(void) //очистка индикаторов
{
  for (uint8_t cnt = 0; cnt < LAMP_NUM; cnt++) indi_buf[cnt] = indi_null;
  indiChangePwm(); //установка Linear Advance
}
//-------------------------Очистка индикатора----------------------------------------------------
void indiClr(uint8_t indi) //очистка индикатора
{
  indi_buf[indi] = indi_null;
  indiChangePwm(); //установка Linear Advance
}
//-------------------------Установка индикатора----------------------------------------------------
void indiSet(uint8_t buf, uint8_t indi) //установка индикатора
{
  indi_buf[indi] = buf;
  indiChangePwm(); //установка Linear Advance
}
//---------------------------------Включение индикаторов---------------------------------------
void indiEnable(void) //включение индикаторов
{
  indiClr(); //очистка индикаторов
  _INDI_ON; //запускаем генерацию
  TCCR1A |= (0x01 << COM1A1); //включаем шим преобразователя
}
//---------------------------------Выключение индикаторов---------------------------------------
void indiDisable(void) //выключение индикаторов
{
  _INDI_OFF; //отключаем генерацию
  for (uint8_t i = 0; i < LAMP_NUM; i++) *anodePort[i] &= ~anodeBit[i]; //сбрасываем аноды
  for (uint8_t i = 0; i < 4; i++) PORTC |= (0x01 << decoderMask[i]); //сбрасываем катоды
  TCCR1A &= ~(0x01 << COM1A1); //выключаем шим преобразователя
  CONV_OFF; //выключаем пин преобразователя
}
//---------------------------------Установка яркости индикатора---------------------------------------
void indiSetBright(uint8_t pwm, uint8_t indi) //установка яркости индикатора
{
  if (pwm > 30) pwm = 30;
  indi_dimm[indi] = pwm << 2;
  indiChangePwm(); //установка Linear Advance
}
//---------------------------------Установка общей яркости---------------------------------------
void indiSetBright(uint8_t pwm) //установка общей яркости
{
  if (pwm > 30) pwm = 30;
  for (byte i = 0; i < LAMP_NUM; i++) indi_dimm[i] = pwm << 2;
  indiChangePwm(); //установка Linear Advance
}
//-------------------------Вывод чисел----------------------------------------------------
void indiPrintNum(uint16_t num, int8_t indi, uint8_t length, char filler) //вывод чисел
{
  uint8_t buf[LAMP_NUM];
  uint8_t st[LAMP_NUM];
  uint8_t c = 0, f = 0;

  if (!num) {
    if (length) {
      for (c = 1; f < (length - 1); f++) st[f] = (filler != ' ') ? filler : 10;
      st[f] = 0;
    }
    else {
      st[0] = 0;
      c = 1;
    }
  }
  else {
    while (num > 0) {
      buf[c] = num % 10;
      num = (num - (num % 10)) / 10;
      c++;
    }

    if (length > c) {
      for (f = 0; f < (length - c); f++) st[f] = (filler != ' ') ? filler : 10;
    }
    for (uint8_t i = 0; i < c; i++) st[i + f] = buf[c - i - 1];
  }

  for (uint8_t cnt = 0; cnt < (c + f); cnt++) {
    uint8_t mergeBuf = 0;
    for (uint8_t dec = 0; dec < 4; dec++) {
      if ((digitMask[st[cnt]] >> dec) & 0x01) mergeBuf |= (0x01 << decoderBit[dec]);
    }
    if (indi < 0) indi++;
    else if (indi < LAMP_NUM) indi_buf[indi++] = mergeBuf;
  }
  indiChangePwm(); //установка Linear Advance
}
