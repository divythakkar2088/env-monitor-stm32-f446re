/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Weather Monitoring System
  *                   BME280 + SSD1306 OLED + RTC + MQ-9
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "fonts.h"
#include "ssd1306.h"
#include "bme280.h"
#include <stdio.h>
#include <string.h>
#include <math.h> 	// powf() for MQ-9 formula
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* MQ-9 sensor constants */
#define MQ9_RL_VALUE       10.0f    /* Load resistor on module (kOhm) */
#define MQ9_RO_CLEAN_AIR    9.8f    /* Sensor resistance in clean air (kOhm) */
#define MQ9_CO_A          599.65f   /* CO curve coefficient a */
#define MQ9_CO_B           -2.1102f /* CO curve exponent b */

/* ADC reference */
#define ADC_VREF            3.3f
#define ADC_RESOLUTION   4095.0f

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
I2C_HandleTypeDef hi2c1;
RTC_HandleTypeDef hrtc;

/* USER CODE BEGIN PV */
struct bme280_dev  dev;
struct bme280_data comp_data;
int8_t rslt;

char hum_string[32];
char temp_string[32];
char press_string[32];
char mq9_string[32];
char datetime_string[32];
char loc_string[] = "Ahmedabad";
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_RTC_Init(void);
static void MX_ADC1_Init(void);

/* USER CODE BEGIN PFP */
int8_t user_i2c_read(uint8_t id, uint8_t reg_addr,
                     uint8_t *data, uint16_t len);
int8_t user_i2c_write(uint8_t id, uint8_t reg_addr,
                      uint8_t *data, uint16_t len);
void   user_delay_ms(uint32_t period);
float  MQ9_ReadRsRoRatio(void);
float  MQ9_GetCO_PPM(float ratio);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* BME280 I2C read callback */
int8_t user_i2c_read(uint8_t id, uint8_t reg_addr,
                     uint8_t *data, uint16_t len)
{
    HAL_StatusTypeDef s = HAL_I2C_Mem_Read(&hi2c1,
                              (uint16_t)(id << 1),
                              reg_addr,
                              I2C_MEMADD_SIZE_8BIT,
                              data, len, HAL_MAX_DELAY);
    return (s == HAL_OK) ? BME280_OK : BME280_E_COMM_FAIL;
}

/* BME280 I2C write callback */
int8_t user_i2c_write(uint8_t id, uint8_t reg_addr,
                      uint8_t *data, uint16_t len)
{
    HAL_StatusTypeDef s = HAL_I2C_Mem_Write(&hi2c1,
                              (uint16_t)(id << 1),
                              reg_addr,
                              I2C_MEMADD_SIZE_8BIT,
                              data, len, HAL_MAX_DELAY);
    return (s == HAL_OK) ? BME280_OK : BME280_E_COMM_FAIL;
}

/* BME280 delay callback */
void user_delay_ms(uint32_t period)
{
    HAL_Delay(period);
}

/* Read MQ-9 ADC and return Rs/Ro ratio.
 * Wiring: MQ-9 AOUT -> 10k -> PA0 -> 10k -> GND
 * PA0 sees max 2.5V (voltage divider halves the 5V sensor output)
 * Formula: Rs = RL x (Vcc - Vsensor) / Vsensor, then ratio = Rs / Ro */
float MQ9_ReadRsRoRatio(void)
{
    uint32_t adc_sum = 0;
    int i;

    /* Take 8 samples and average for stability */
    for (i = 0; i < 32; i++)
    {
        HAL_ADC_Start(&hadc1);
        HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
        adc_sum += HAL_ADC_GetValue(&hadc1);
        HAL_ADC_Stop(&hadc1);
        HAL_Delay(1);
    }

    float adc_avg  = (float)(adc_sum / 50);
    float v_pa0    = (adc_avg / ADC_RESOLUTION) * ADC_VREF;
    float v_sensor = v_pa0 * 2.0f; /* recover true sensor voltage */

    if (v_sensor < 0.001f) v_sensor = 0.001f;

    float rs = MQ9_RL_VALUE * (5.0f - v_sensor) / v_sensor;
    if (rs < 0.001f) rs = 0.001f;

    return rs / MQ9_RO_CLEAN_AIR;
}

/* Convert Rs/Ro ratio to CO concentration in ppm.
 * Uses power-law fit from Hanwei MQ-9 CO sensitivity curve:
 * ppm = 599.65 x (Rs/Ro)^(-2.1102)
 * Valid range: approximately 10 to 1000 ppm */
float MQ9_GetCO_PPM(float ratio)
{
    float ppm;
    if (ratio <= 0.0f) return 0.0f;
    ppm = MQ9_CO_A * powf(ratio, MQ9_CO_B);
    if (ppm < 0.0f)    ppm = 0.0f;
    if (ppm > 9999.0f) ppm = 9999.0f;
    return ppm;
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* MCU Configuration -------------------------------------------------------*/
  HAL_Init();

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_RTC_Init();
  MX_ADC1_Init();

  /* USER CODE BEGIN 2 */

  /* Initialize OLED display */
  SSD1306_Init();
  SSD1306_Fill(0);
  SSD1306_UpdateScreen();
  HAL_Delay(500);

  /* Initialize BME280 */
  dev.dev_id   = BME280_I2C_ADDR_PRIM;
  dev.intf     = BME280_I2C_INTF;
  dev.read     = user_i2c_read;
  dev.write    = user_i2c_write;
  dev.delay_ms = user_delay_ms;

  rslt = bme280_init(&dev);
  if (rslt != BME280_OK) { while(1){} }

  dev.settings.osr_h  = BME280_OVERSAMPLING_1X;
  dev.settings.osr_p  = BME280_OVERSAMPLING_16X;
  dev.settings.osr_t  = BME280_OVERSAMPLING_2X;
  dev.settings.filter = BME280_FILTER_COEFF_16;

  rslt = bme280_set_sensor_settings(
             BME280_OSR_PRESS_SEL | BME280_OSR_TEMP_SEL |
             BME280_OSR_HUM_SEL   | BME280_FILTER_SEL, &dev);
  if (rslt != BME280_OK) { while(1){} }

  rslt = bme280_set_sensor_mode(BME280_SLEEP_MODE, &dev);
  if (rslt != BME280_OK) { while(1){} }

  RTC_TimeTypeDef sTime;
  RTC_DateTypeDef sDate;

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
  /* USER CODE END WHILE */

  /* USER CODE BEGIN 3 */

    /* Read RTC time and date
     * Note: GetDate must always be called after GetTime
     * to unlock the shadow registers */
    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    snprintf(datetime_string, sizeof(datetime_string),
             "%02d:%02d:%02d %02d/%02d/%02d",
             sTime.Hours,  sTime.Minutes, sTime.Seconds,
             sDate.Date,   sDate.Month,   sDate.Year);

    /* Read MQ-9 and calculate CO in ppm */
    float mq9_ratio = MQ9_ReadRsRoRatio();
    float co_ppm    = MQ9_GetCO_PPM(mq9_ratio);

    snprintf(mq9_string, sizeof(mq9_string), "CO:   %.0f ppm", co_ppm);

    /* Trigger BME280 in forced mode and read all sensor data */
    rslt = bme280_set_sensor_mode(BME280_FORCED_MODE, &dev);

    if (rslt == BME280_OK)
    {
      HAL_Delay(50);

      rslt = bme280_get_sensor_data(BME280_ALL, &comp_data, &dev);

      if (rslt == BME280_OK)
      {
        float temperature = (float)comp_data.temperature;
        float humidity    = (float)comp_data.humidity;
        float pressure    = (float)comp_data.pressure;

        snprintf(hum_string,   sizeof(hum_string),
                 "Hum:  %.1f %%",  humidity);
        snprintf(temp_string,  sizeof(temp_string),
                 "Temp: %.1f C",   temperature);
        snprintf(press_string, sizeof(press_string),
                 "Pres: %.0f Pa",  pressure);

        /* Update OLED -- 128x64, Font_6x8, 9px row pitch
         * Y= 0  Weather Monitor
         * Y= 9  HH:MM:SS DD/MM/YY
         * Y=18  Hum:  xx.x %
         * Y=27  Temp: xx.x C
         * Y=36  Pres: xxxxxx Pa
         * Y=45  CO:   xxx ppm
         * Y=54  Ahmedabad */
        SSD1306_Fill(0);

        SSD1306_GotoXY(2,  0);
        SSD1306_Puts("Weather Monitoring", &Font_6x8, 1);
        SSD1306_GotoXY(2,  9);
        SSD1306_Puts(datetime_string,   &Font_6x8, 1);
        SSD1306_GotoXY(2, 18);
       // SSD1306_Puts(hum_string,        &Font_6x8, 1);
        SSD1306_GotoXY(2, 27);
        SSD1306_Puts(temp_string,       &Font_6x8, 1);
        SSD1306_GotoXY(2, 36);
        SSD1306_Puts(press_string,      &Font_6x8, 1);
        SSD1306_GotoXY(2, 45);
        SSD1306_Puts(mq9_string,        &Font_6x8, 1);
        SSD1306_GotoXY(2, 54);
        SSD1306_Puts(loc_string,        &Font_6x8, 1);

        SSD1306_UpdateScreen();
      }
    }

    HAL_Delay(1000);

  /* USER CODE END 3 */
  }
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef       RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef       RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit     = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /* Use HSI for system clock and LSI for RTC.
   * Nucleo F446RE has no LSE crystal fitted by default. */
  RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI |
                                          RCC_OSCILLATORTYPE_LSI;
  RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.LSIState            = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM            = 16;
  RCC_OscInitStruct.PLL.PLLN            = 336;
  RCC_OscInitStruct.PLL.PLLP            = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ            = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) { Error_Handler(); }

  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC;
  PeriphClkInit.RTCClockSelection    = RCC_RTCCLKSOURCE_LSI;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) { Error_Handler(); }

  __HAL_RCC_RTC_ENABLE();

  RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK   |
                                     RCC_CLOCKTYPE_SYSCLK |
                                     RCC_CLOCKTYPE_PCLK1  |
                                     RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{
  /* USER CODE BEGIN ADC1_Init 0 */
  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */
  /* USER CODE END ADC1_Init 1 */

  hadc1.Instance                   = ADC1;
  hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode          = DISABLE;
  hadc1.Init.ContinuousConvMode    = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion       = 1;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK) { Error_Handler(); }

  /* Configure ADC channel 0 on PA0 for MQ-9 analog output */
  sConfig.Channel      = ADC_CHANNEL_0;
  sConfig.Rank         = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_144CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) { Error_Handler(); }

  /* USER CODE BEGIN ADC1_Init 2 */
  /* USER CODE END ADC1_Init 2 */
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{
  /* USER CODE BEGIN I2C1_Init 0 */
  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */
  /* USER CODE END I2C1_Init 1 */

  hi2c1.Instance             = I2C1;
  hi2c1.Init.ClockSpeed      = 400000;
  hi2c1.Init.DutyCycle       = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1     = 0;
  hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2     = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK) { Error_Handler(); }

  /* USER CODE BEGIN I2C1_Init 2 */
  /* USER CODE END I2C1_Init 2 */
}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{
  /* USER CODE BEGIN RTC_Init 0 */
  /* USER CODE END RTC_Init 0 */

  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};

  /* USER CODE BEGIN RTC_Init 1 */
  /* USER CODE END RTC_Init 1 */

  hrtc.Instance            = RTC;
  hrtc.Init.HourFormat     = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv   = 127;
  hrtc.Init.SynchPrediv    = 255;
  hrtc.Init.OutPut         = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType     = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK) { Error_Handler(); }

  /* USER CODE BEGIN Check_RTC_BKUP */
  /* USER CODE END Check_RTC_BKUP */

  /* Set initial time and date using BIN format (plain decimal values) */
  sTime.Hours          = 10;
  sTime.Minutes        = 30;
  sTime.Seconds        = 0;
  sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sTime.StoreOperation = RTC_STOREOPERATION_RESET;
  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK)
  {
    Error_Handler();
  }

  sDate.WeekDay = RTC_WEEKDAY_THURSDAY;
  sDate.Month   = RTC_MONTH_MARCH;
  sDate.Date    = 13;
  sDate.Year    = 26; /* 2026 */
  if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN RTC_Init 2 */
  /* USER CODE END RTC_Init 2 */
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* USER CODE BEGIN MX_GPIO_Init_1 */
  /* USER CODE END MX_GPIO_Init_1 */

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /* B1 user button */
  GPIO_InitStruct.Pin  = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /* USART2 TX/RX pins */
  GPIO_InitStruct.Pin       = USART_TX_Pin | USART_RX_Pin;
  GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull      = GPIO_NOPULL;
  GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* LD2 green LED */
  GPIO_InitStruct.Pin   = LD2_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1) {}
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name and line number where assert_param error occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  (void)file;
  (void)line;
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

