#include "lpc17xx_pinsel.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_i2c.h"
#include "lpc17xx_ssp.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_adc.h"
#include "lpc17xx_rtc.h"
#include "lpc17xx.h"

#include "led7seg.h"
#include "rotary.h"
#include "pca9532.h"
#include "joystick.h"
#include "rgb.h"
#include "oled.h"
#include "light.h"

static uint8_t buf[10];
static int x = 0;
// Initial value passed to timer
static int32_t t = 10;

static uint32_t RTC_godziny = 12;
static uint32_t RTC_minuty = 59;
static uint32_t RTC_sekundy = 55;

#define NOTE_PIN_HIGH() GPIO_SetValue(0, ((uint32_t)1 << 26));
#define NOTE_PIN_LOW() GPIO_ClearValue(0, ((uint32_t)1 << 26));

#define PWMENA6 ((uint8_t)1 << 14)
#define LER6_EN ((uint8_t)1 << 6)
#define PWMMR0I ((uint8_t)1 << 0)
#define TCR_CNT_EN 0x00000001
#define TCR_RESET 0x00000002
#define TCR_PWM_EN 0x00000008
#define PWMPRESCALE (25 - 1)

static void pwm_init(uint32_t cycle)
{
    LPC_PINCON->PINSEL4 |= ((uint16_t)0x01 << 10);
    LPC_PWM1->TCR = TCR_RESET;
    LPC_PWM1->PR = 0x00;
    LPC_PWM1->MCR = PWMMR0I;
    LPC_PWM1->MR0 = cycle;
    LPC_PWM1->LER = LER6_EN;
}

static void pwm_set(uint32_t offset)
{
    LPC_PWM1->MR6 = offset;
    LPC_PWM1->LER = LER6_EN;
}

static void pwm_start(void)
{
    /* All single edge, all enable */
    LPC_PWM1->PCR = (uint32_t)PWMENA6;
    LPC_PWM1->TCR = TCR_CNT_EN | TCR_PWM_EN; /* counter enable, PWM enable */
}

static void setUpRTC(void)
{
    /* RealTime Clock */
    RTC_Init(LPC_RTC);
    RTC_ResetClockTickCounter(LPC_RTC);
    RTC_Cmd(LPC_RTC, ENABLE);

    // ustaw godzine
    RTC_SetTime(LPC_RTC, RTC_TIMETYPE_HOUR, RTC_godziny);
    RTC_SetTime(LPC_RTC, RTC_TIMETYPE_MINUTE, RTC_minuty);
    RTC_SetTime(LPC_RTC, RTC_TIMETYPE_SECOND, RTC_sekundy);
    /* RealTime Clock */
}

static uint32_t notes[] = {
    2272, // A - 440 Hz
    2024, // B - 494 Hz
    3816, // C - 262 Hz
    3401, // D - 294 Hz
    3030, // E - 330 Hz
    2865, // F - 349 Hz
    2551, // G - 392 Hz
    1136, // a - 880 Hz
    1012, // b - 988 Hz
    1912, // c - 523 Hz
    1703, // d - 587 Hz
    1517, // e - 659 Hz
    1432, // f - 698 Hz
    1275, // g - 784 Hz
};

static const char *songs[] = {
    "E2,E2,E2,E2",
};

static void playNote(uint32_t note, uint32_t durationMs)
{

    uint32_t t = 0;

    if (note > (uint32_t)0)
    {

        while ((uint32_t)t < ((uint32_t)durationMs * (uint32_t)1000))
        {
            NOTE_PIN_HIGH();
            Timer0_us_Wait(note / (uint32_t)2);
            // delay32Us(0, note / 2);

            NOTE_PIN_LOW();
            Timer0_us_Wait(note / (uint32_t)2);
            // delay32Us(0, note / 2);

            t += note;
        }
    }
    else
    {
        Timer0_Wait(durationMs);
        // delay32Ms(0, durationMs);
    }
}

static uint32_t getNote(uint8_t ch)
{
	uint32_t res;
    if (ch >= (uint32_t)'A' && ch <= (uint32_t)'G'){
    	res = notes[ch - (uint32_t)'A'];
    }

    if (ch >= (uint32_t)'a' && ch <= (uint32_t)'g'){
    	res = notes[ch - (uint32_t)'a' + (uint32_t)7];
    }

    return res;
}

static uint32_t getDuration(uint8_t ch)
{
	uint32_t res;
    if (ch < (uint32_t)'0' || ch > (uint32_t)'9'){
        res = 400;
    }

    /* number of ms */
    res = (ch - (uint32_t)'0') * (uint32_t)200;
    return res;
}

static uint32_t getPause(uint8_t ch)
{
	uint32_t res;
    switch ((int)ch)
    {
    case '+':
    	res = 0;
    	break;
    case ',':
    	res = 5;
    	break;
    case '.':
    	res = 20;
    	break;
    case '_':
    	res = 30;
    	break;
    default:
    	res = 5;
    }

    return res;
}

static void playSong(uint8_t *song)
{
    uint32_t note = 0;
    uint32_t dur = 0;
    uint32_t pause = 0;

    /*
     * A song is a collection of tones where each tone is
     * a note, duration and pause, e.g.
     *
     * "E2,F4,"
     */

    while (*song != '\0')
    {
        note = getNote(*song++);
        if (*song == '\0'){
            break;
        }
        dur = getDuration(*song++);
        if (*song == '\0'){
            break;
        }
        pause = getPause(*song++);

        playNote(note, dur);
        // delay32Ms(0, pause);
        Timer0_Wait(pause);
    }
}


static void init_ssp(void)
{
    PINSEL_CFG_Type PinCfg;
    SSP_CFG_Type SSP_ConfigStruct;

    /*
     * Initialize SPI pin connect
     * P0.7 - SCK;
     * P0.8 - MISO
     * P0.9 - MOSI
     * P2.2 - SSEL - used as GPIO
     */
    PinCfg.Funcnum = 2;
    PinCfg.OpenDrain = 0;
    PinCfg.Pinmode = 0;
    PinCfg.Portnum = 0;
    PinCfg.Pinnum = 7;
    PINSEL_ConfigPin(&PinCfg);
    PinCfg.Pinnum = 8;
    PINSEL_ConfigPin(&PinCfg);
    PinCfg.Pinnum = 9;
    PINSEL_ConfigPin(&PinCfg);
    PinCfg.Funcnum = 0;
    PinCfg.Portnum = 2;
    PinCfg.Pinnum = 2;
    PINSEL_ConfigPin(&PinCfg);

    SSP_ConfigStructInit(&SSP_ConfigStruct);

    // Initialize SSP peripheral with parameter given in structure above
    SSP_Init(LPC_SSP1, &SSP_ConfigStruct);

    // Enable SSP peripheral
    SSP_Cmd(LPC_SSP1, ENABLE);
}

static void init_i2c(void)
{
    PINSEL_CFG_Type PinCfg;

    /* Initialize I2C2 pin connect */
    PinCfg.Funcnum = 2;
    PinCfg.Pinnum = 10;
    PinCfg.Portnum = 0;
    PINSEL_ConfigPin(&PinCfg);
    PinCfg.Pinnum = 11;
    PINSEL_ConfigPin(&PinCfg);

    // Initialize I2C peripheral
    I2C_Init(LPC_I2C2, 100000);

    /* Enable I2C1 operation */
    I2C_Cmd(LPC_I2C2, ENABLE);
}

static void init_sound(void)
{
    GPIO_SetDir(2, (uint32_t)1 << 0, 1);
    GPIO_SetDir(2, (uint32_t)1 << 1, 1);

    GPIO_SetDir(0, (uint32_t)1 << 27, 1);
    GPIO_SetDir(0, (uint32_t)1 << 28, 1);
    GPIO_SetDir(2, (uint32_t)1 << 13, 1);
    GPIO_SetDir(0, (uint32_t)1 << 26, 1);

    GPIO_ClearValue(0, (uint32_t)1 << 27); // LM4811-clk
    GPIO_ClearValue(0, (uint32_t)1 << 28); // LM4811-up/dn
    GPIO_ClearValue(2, (uint32_t)1 << 13); // LM4811-shutdn
}

static void init_adc(void)
{
    PINSEL_CFG_Type PinCfg;

    /*
     * Init ADC pin connect
     * AD0.0 on P0.23
     */
    PinCfg.Funcnum = 1;
    PinCfg.OpenDrain = 0;
    PinCfg.Pinmode = 0;
    PinCfg.Pinnum = 23;
    PinCfg.Portnum = 0;
    PINSEL_ConfigPin(&PinCfg);

    /* Configuration for ADC :
     * 	Frequency at 0.2Mhz
     *  ADC channel 0, no Interrupt
     */
    ADC_Init(LPC_ADC, 200000);
    ADC_IntConfig(LPC_ADC, ADC_CHANNEL_0, DISABLE);
    ADC_ChannelCmd(LPC_ADC, ADC_CHANNEL_0, ENABLE);
}

static int how_many_seconds(int h, int m, int s)
{
    return ((h * 60 * 60) + (m * 60) + s);
}

static void show_time(void)
{
    int minutes = t / 60;
    int sec = t % 60;
    char minutes_string[2];
    char seconds_string[2];
    if (t / 60 < 10)
    {
        x = snprintf(minutes_string, 12, "0%d", minutes);
    }
    else
    {
        x = snprintf(minutes_string, 12, "%d", minutes);
    }

    if (t % 60 < 10)
    {
        x = snprintf(seconds_string, 12, "0%d", sec);
    }
    else
    {
        x = snprintf(seconds_string, 12, "%d", sec);
    }

    char time_string[12];
    x = snprintf(time_string, 12, "%s:%s", minutes_string, seconds_string);
    //				oled_fillRect((1+7*6),1, 80, 8, OLED_COLOR_WHITE);
    oled_putString((1 + 7 * 6), 1, time_string, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
}

int main(void)
{

    uint8_t state = 0;
    char ch = '0';
    uint8_t btn1 = 0;
    uint32_t lux = 0;
    int min_motor = 300;
    int timestamp = 0;
    int offset_value = min_motor;

    rotary_init();
    led7seg_init();

    pca9532_init();
    init_i2c();
    init_ssp();
    init_adc();
    setUpRTC();

    oled_init();
    light_init();
    light_enable();
    light_setRange(LIGHT_RANGE_4000);

    init_sound();
    pwm_init(1000);
    pwm_set(min_motor);
    pwm_start();

    pca9532_setLeds(1, 2);

    rgb_init();
    joystick_init();
    oled_clearScreen(OLED_COLOR_WHITE);

    uint8_t joy = 0;
    int isOn = 0;

    int isOnCooldown = 0;
    int timerCooldown = 0;
    uint16_t ledsOn = 1;

    led7seg_setChar(ch, FALSE);

    while (1)
    {

        RTC_sekundy = RTC_GetTime(LPC_RTC, RTC_TIMETYPE_SECOND);
        RTC_minuty = RTC_GetTime(LPC_RTC, RTC_TIMETYPE_MINUTE);
        RTC_godziny = RTC_GetTime(LPC_RTC, RTC_TIMETYPE_HOUR);

        joy = joystick_read();

        btn1 = ((GPIO_ReadValue(0) >> 4) & 0x01);
        if ((int)btn1 == 0 && isOnCooldown <= 0)
        {
            offset_value += 100;
            ledsOn = ledsOn << 1;
            ledsOn += (uint8_t)1;
            if (ledsOn > (uint8_t)255)
            {
                ledsOn = 1;
                offset_value = min_motor;
            }
            if (isOn != 0)
            {
                pwm_set(offset_value);
            }
            pca9532_setLeds(ledsOn, 255);
            isOnCooldown = 25000;
        }

        if ((joy & JOYSTICK_CENTER) != 0 && isOnCooldown <= 0)
        {
            if (isOn != 0)
            {
                rgb_setLeds(RGB_RED | RGB_GREEN | 0);
                isOn = 0;
                pwm_set(min_motor);
            }
            else
            {
                if (t > 0)
                {
                    rgb_setLeds(0 | RGB_GREEN | RGB_BLUE);
                    timestamp = how_many_seconds(RTC_godziny, RTC_minuty, RTC_sekundy) + t;
                    isOn = 1;
                    pwm_set(offset_value);
                }
            }

            isOnCooldown = 100000;
        }
        if (isOn == 0)
        {
            if ((joy & JOYSTICK_LEFT) != 0 && isOnCooldown <= 0)
            {
                if (t > 0)
                {
                    t--;
                }

                show_time();
                isOnCooldown = 25000;
            }
            if ((joy & JOYSTICK_RIGHT) != 0 && isOnCooldown <= 0)
            {
                t++;
                show_time();
                isOnCooldown = 25000;
            }

            if ((joy & JOYSTICK_DOWN) != 0 && isOnCooldown <= 0)
            {
                if (t >= 10)
                {
                    t -= 10;
                }
                show_time();
                isOnCooldown = 25000;
            }
            if ((joy & JOYSTICK_UP) != 0 && isOnCooldown <= 0)
            {
                t += 10;
                show_time();
                isOnCooldown = 25000;
            }
        }
        if (isOnCooldown > 0)
        {
            isOnCooldown--;
        }
        state = rotary_read();
        if (state != ROTARY_WAIT && isOn != 1)
        {

            if (state == ROTARY_RIGHT)
            {
                ch += 1;
            }
            else
            {
                ch -= 1;
            }

            if (ch > '9')
            {
                ch = '0';
            }
            else if (ch < '0')
            {
                ch = '9';
            }
            else
            {

            }


            led7seg_setChar(ch, FALSE);
        }

        if (timerCooldown == 0)
        {
            lux = light_read();

            //		   oled_clearScreen(OLED_COLOR_WHITE);
//            uint8_t text[7] = "Timer :";
            oled_putString(1, 1, (uint8_t *)"Timer :", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
            //		   oled_putString(1,10,  (uint8_t*)"Moc   :", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
            //		   oled_putString(1,19,  (uint8_t*)"Czas  :", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
            //		   oled_putString(1,28,  (uint8_t*)"p     :", OLED_COLOR_BLACK, OLED_COLOR_WHITE);

            /* output values to OLED display */

            show_time();

            //			   intToString(ch-48, buf, 10, 10);
            ////			   oled_fillRect((1+7*6),10, 80, 17, OLED_COLOR_WHITE);
            //			   oled_putString((1+7*6),10, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);

            x = snprintf(buf, 12, "%02d:%02d:%02d", RTC_godziny, RTC_minuty, RTC_sekundy);
            //		    	oled_fillRect((1+7*6),19, 80, 28, OLED_COLOR_WHITE);
            oled_putString((1 + 7 * 3), 50, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);

            if (isOn == 1)
            {

                if (t <= 0)
                {
                    rgb_setLeds(RGB_RED | RGB_GREEN | 0);
                    isOn = 0;
                    ;
                    pwm_set(min_motor);
                    if (lux > (uint32_t)30)
                    {
                        playSong(*songs);
                    }
                }
                t = timestamp - how_many_seconds(RTC_godziny, RTC_minuty, RTC_sekundy);
            }
            /* delay */
            timerCooldown = 25000;
        }
        if (timerCooldown > 0)
        {
            timerCooldown--;
        }
    }
}
