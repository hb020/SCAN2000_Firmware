/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2021 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

// if LOG_DEBUG_MESSAGES is set:
// * logs some additional debug info
// * uses a rather long clock signal idle timeout 
// * lights the Activity led only during interrupt handler activity, so you can use it 
//   investigate interrupt handling duration (and hangs) 
// When not set, the activity led toggles ar each received message.
#define LOG_DEBUG_MESSAGES

// stupid workaround since printing uint64_t with %llx results in "lx" to be printed
#define PRI_UINT64_C_Val(value) ((unsigned long) (value>>32)),((unsigned long)value)
#define PRI_UINT64 "%08lx %08lx"

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart4;
DMA_HandleTypeDef hdma_usart4_tx;

/* USER CODE BEGIN PV */
// set by both the interrupt handler and the main loop, used by the main loop
volatile uint32_t timeSinceLastClock = 0;
// below are all maintained and used by the interrupt handler. 
uint64_t receivedSequence = 0;
volatile uint8_t receivedCounter = 0; // also read by the main loop
uint32_t channelState = 0;

// This is where the pins are. This is not used for the init, so if you change anything, also change in the init.
#define NR_CHANNELS 20
GPIO_TypeDef* GPIOsequence[NR_CHANNELS] = {CH1_GPIO_Port, CH2_GPIO_Port, CH3_GPIO_Port, CH4_GPIO_Port, CH5_GPIO_Port, CH6_GPIO_Port, CH7_GPIO_Port, CH8_GPIO_Port, CH9_GPIO_Port, CH10_GPIO_Port, CH11_GPIO_Port, CH12_GPIO_Port, CH13_GPIO_Port, CH14_GPIO_Port, CH15_GPIO_Port, CH16_GPIO_Port, CH17_GPIO_Port, CH18_GPIO_Port, CH19_GPIO_Port, CH20_GPIO_Port};
uint32_t PinSequence[NR_CHANNELS] = {CH1_Pin, CH2_Pin, CH3_Pin, CH4_Pin, CH5_Pin, CH6_Pin, CH7_Pin, CH8_Pin, CH9_Pin, CH10_Pin, CH11_Pin, CH12_Pin, CH13_Pin, CH14_Pin, CH15_Pin, CH16_Pin, CH17_Pin, CH18_Pin, CH19_Pin, CH20_Pin};

// channelState is 32 bit, and is organised in sequence: 0 = CH1 ... 19=CH20, 20 = 4W
// The following bitmasks are for the channelState
#define CHANNELSTATE_BITMASK_BANK1 0x003FF
#define CHANNELSTATE_BITMASK_BANK2 0xFFC00
#define CHANNELSTATE_BITMASK_4W 0x100000

#define SCAN2000_20_BITS 24
// This is the sequence of the command on a -20 board. 2 bits per channel. Even bits turn off, odd bits turn on.
// The command is 48 bits (24 relays), but only the first 21 are used: 20 channels (banks inverted), plus 4W. The "Bank 2 to OUT", "Bank 2 to 4W" are derived from that 4W bit.
// This array lists the use of the bits for the channels: 1..20 for the channels, 21 for the 4W, 22..24 for spare (future-proof debugging)
uint8_t scan2000_20ChannelSequence[SCAN2000_20_BITS] = {11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 1, 2, 3, 4, 5, 6, 7 , 8, 9, 10, 21, 22, 23, 24};    // CH11..CH20, CH1..CH10, 4W, 3 spares

#define BITS_FOR_SCAN2000_STD 12
// TODO test and finalise this

uint8_t scan2000ChannelOffSequence[] = {17, 19, 21, 23, 8, 14, 0, 2, 4, 5, 12};      // CH1..CH10, 4W
uint8_t scan2000ChannelOnSequence[] = {16, 18, 20, 22, 9, 13, 15, 1, 3, 6, 11};      // CH1..CH10, 4W


// The interrupt handler IS NOT ALLOWED TO BLOCK. Therefore, I log my messages in a buffer that I print out from the main loop.
struct msgInfo {
  enum {msgUnknown = 0, msgOK, msgIgnored, msgLengthError, msgDataError, msgRelayError} state;
  uint8_t receivedCounter;
  uint64_t receivedSequence;
  uint32_t channelState;
  uint32_t timestamp;
};

// circular buffer for messages, used between interrupt handler and main loop
struct msgInfo msgBuffer[256]; // MUST BE 256 (or more), uint8_t max. Just forgot what is the preprocessor define for that.
volatile uint8_t msgReadLevel; // to be set only from the main loop.
volatile uint8_t msgWriteLevel; // to be set only from the interrupt handler

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART4_UART_Init(void);
/* USER CODE BEGIN PFP */

static void MsgBuffer_Init(void);
static void MsgBuffer_add(struct msgInfo msg); // to be called only from the interrupt handler
static void MsgBuffer_print(void);

#ifdef __GNUC__
/* With GCC/RAISONANCE, small printf (option LD Linker->Libraries->Small printf
   set to 'Yes') calls __io_putchar() */
#define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
extern int __io_putchar(int ch) __attribute__((weak));
#else
#define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif /* __GNUC__ */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART4_UART_Init();
  MsgBuffer_Init();
  /* USER CODE BEGIN 2 */
  //__HAL_DMA_DISABLE_IT(huart4.hdmarx, DMA_IT_HT); // Disable Half Transfer Interrupt
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);  // turn on the LED
  printf("\n===============\nBooting, SCAN2000-20 FW version " FW_VERSION "\n");
  for (uint8_t i=0; i < 6; i++) {
    HAL_Delay(100); // sleep for 100 ms
    HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
  }
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET); // led off
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN 3 */
#ifdef LOG_DEBUG_MESSAGES   
  printf("Log level: DEBUG\n");
#endif 

  // local variables for the idle detection loop
  uint32_t old_timeSinceLastClock;
  uint32_t now_timeSinceLastClock;
  uint8_t old_receivedCounter;
  uint8_t now_receivedCounter;   
  now_timeSinceLastClock = timeSinceLastClock;
  now_receivedCounter = receivedCounter;
  old_timeSinceLastClock = now_timeSinceLastClock;
  old_receivedCounter = now_receivedCounter;

  while (1)
  {
    MsgBuffer_print();
     
    // Detect idle situations
    // Normally clock period is 10us, and strobe follows within 20us.
    // The handling of the clock and strobe, and the update of timeSinceLastClock 
    // is inside the interrupt handler (HAL_GPIO_EXTI_Rising_Callback)
    // TODO: For some reason, if I do not print idle messages, the MsgBuffer_print does not print. Fix that.
#ifdef LOG_DEBUG_MESSAGES
// 5 sec idle timeout for debugging
#define IDLE_TIMEOUT 5000

    // The following is a bit strange. Since I base my idle state on variables that are maintained
    // in the interrupt handler, and there are no atomic operations on those variables, and 
    // there is no sync or lock between the interrupt handler and the main loop, the variables read here
    // can be changed while reading, and therefore can be wrong.
    // To avoid that, I just read them 2 times, and if both reads are the same, then they should be OK.
    // 
    // first hold on to old values
    old_timeSinceLastClock = now_timeSinceLastClock;
    old_receivedCounter = now_receivedCounter;
    // then get the present ones 
    now_timeSinceLastClock = timeSinceLastClock;
    now_receivedCounter = receivedCounter;

    // only look at idle state if nothing is moving. 
    if ((now_timeSinceLastClock == old_timeSinceLastClock) && (now_receivedCounter == old_receivedCounter)) {

      uint32_t now = HAL_GetTick();

      if (now - now_timeSinceLastClock > IDLE_TIMEOUT) { 
          timeSinceLastClock = now; // reset to avoid message storms. This is NOT safe to do, for the same reasons that the reading is not safe. But making that safe would be rather complicated fpor little gain.
          if (now_receivedCounter) {
  #ifdef LOG_DEBUG_MESSAGES
            printf("%lu - WARNING - IDLE TIMEOUT detected with %u clocks received. This might provoke problems, but it may also be a false alert.\n",now, (unsigned int)now_receivedCounter);
            // I will not clear receivedCounter, as that could provoke a lot of problems.
            // Any real errors will be handled in the interrupt handler.
          } else {
            printf("%lu - DEBUG - Idle\n",now);
  #endif
          }
      }
    }

#endif    
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV1;
  RCC_OscInitStruct.PLL.PLLN = 8;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART4_UART_Init(void)
{

  /* USER CODE BEGIN USART4_Init 0 */

  /* USER CODE END USART4_Init 0 */

  /* USER CODE BEGIN USART4_Init 1 */

  /* USER CODE END USART4_Init 1 */
  huart4.Instance = USART4;
  huart4.Init.BaudRate = 115200;
  huart4.Init.WordLength = UART_WORDLENGTH_8B;
  huart4.Init.StopBits = UART_STOPBITS_1;
  huart4.Init.Parity = UART_PARITY_NONE;
  huart4.Init.Mode = UART_MODE_TX;
  huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart4.Init.OverSampling = UART_OVERSAMPLING_16;
  huart4.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart4.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart4.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_HalfDuplex_Init(&huart4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART4_Init 2 */

  /* USER CODE END USART4_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);

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

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, LED_Pin|CH20_Pin|CH19_Pin|CH18_Pin
                          |CH17_Pin|CH16_Pin|CH15_Pin|CH11_Pin
                          |Bus_Sense_Pin|CH1_Pin|CH2_Pin|CH3_Pin
                          |CH4_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, CH14_Pin|CH13_Pin|CH12_Pin|CH5_Pin
                          |CH6_Pin|CH7_Pin|CH8_Pin|CH9_Pin
                          |CH10_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(Bus_In_GPIO_Port, Bus_In_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : Data_Pin */
  GPIO_InitStruct.Pin = Data_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(Data_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : Clock_Pin Strobe_Pin */
  GPIO_InitStruct.Pin = Clock_Pin|Strobe_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : LED_Pin */
  GPIO_InitStruct.Pin = LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(LED_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : CH20_Pin CH19_Pin CH18_Pin CH17_Pin
                           CH16_Pin CH15_Pin CH11_Pin Bus_Sense_Pin
                           CH1_Pin CH2_Pin CH3_Pin CH4_Pin */
  GPIO_InitStruct.Pin = CH20_Pin|CH19_Pin|CH18_Pin|CH17_Pin
                          |CH16_Pin|CH15_Pin|CH11_Pin|Bus_Sense_Pin
                          |CH1_Pin|CH2_Pin|CH3_Pin|CH4_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : CH14_Pin CH13_Pin CH12_Pin CH5_Pin
                           CH6_Pin CH7_Pin CH8_Pin CH9_Pin
                           CH10_Pin */
  GPIO_InitStruct.Pin = CH14_Pin|CH13_Pin|CH12_Pin|CH5_Pin
                          |CH6_Pin|CH7_Pin|CH8_Pin|CH9_Pin
                          |CH10_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : Bus_In_Pin */
  GPIO_InitStruct.Pin = Bus_In_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(Bus_In_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI4_15_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
static void MsgBuffer_Init(void) {
  memset(msgBuffer,0, sizeof(msgBuffer));
  msgWriteLevel = 0;
  msgReadLevel = 0;
}

/*
 * Add a merssage to the message queue.
 * to be called only from the interrupt handler.
 */
static void MsgBuffer_add(struct msgInfo msg) {
  memcpy(&(msgBuffer[msgWriteLevel]),&msg,sizeof(msg));
  msgWriteLevel++;
}

static void MsgBuffer_print(void) {
  struct msgInfo msg;
  // although the protocol only handles 21 bits, I just show all 24 for debugging purposes
  char outputstate[SCAN2000_20_BITS+2+1]; // 24 + 2 spaces + null
  while (msgReadLevel != msgWriteLevel) {
    memcpy(&msg, &(msgBuffer[msgReadLevel]),sizeof(msg));
    printf("%lu - ", msg.timestamp);
    int offset = 0;
    for (uint8_t i=0; i<SCAN2000_20_BITS; i++) {
      bool b = msg.channelState & (1 << i);
      if ((i == 10) || (i == 20)) {
        outputstate[i+offset] = ' ';
        offset++;
      }
      if (b)
        outputstate[i+offset] = '1';
      else
        outputstate[i+offset] = '0';
    }
    outputstate[sizeof(outputstate) - 1] = 0;
    if (msg.state == msgOK) {
      printf("INFO - OK - %u bit command : 0x" PRI_UINT64 ". Channel state => %s\n", (unsigned int)msg.receivedCounter, PRI_UINT64_C_Val(msg.receivedSequence), outputstate);
    } else if (msg.state == msgIgnored) {
      printf("INFO - IG - %u bit NULL command, ignored\n", (unsigned int)msg.receivedCounter);
    } else if (msg.state == msgLengthError) {
      printf("ERROR - Message of invalid length - %u bit command, dropping.\n", (unsigned int)msg.receivedCounter);
    } else if (msg.state == msgDataError) {
      printf("ERROR - Invalid command - %u bit command : 0x" PRI_UINT64 ", dropping.\n", (unsigned int)msg.receivedCounter, PRI_UINT64_C_Val(msg.receivedSequence));
    } else if (msg.state == msgRelayError) {
      printf("ERROR - Invalid relay state - %u bit command : 0x" PRI_UINT64 ". Relay state wanted: %s, dropping\n", (unsigned int)msg.receivedCounter, PRI_UINT64_C_Val(msg.receivedSequence), outputstate);
    } else {
      printf("ERROR - Unknown error - %u bit command : 0x" PRI_UINT64 ". Relay state wanted: %s, dropping\n", (unsigned int)msg.receivedCounter, PRI_UINT64_C_Val(msg.receivedSequence), outputstate);
    }
    msgReadLevel++;
  }
}

typedef enum decodeResult_t {decodeOK = 0, decodeIgnored, decodeDataError, decodeLengthError} decodeResult_t;

/**
 * @brief Decode 10 channel command
 * The K2002 clocks out commands at a 2.8 ms interval
 * @param command Command received
 * @param relaySetRegister ptr to bitmap for setting relays
 * @param relayUnsetRegister ptr to bitmap for unsetting relays
 * @return decodeResult_t
 */
decodeResult_t decode_10channels(uint32_t command, uint32_t *relaySetRegister, uint32_t *relayUnsetRegister) {
    decodeResult_t rv;
    // Check always high bits
    if ((command & SCAN_2000_ALWAYS_HIGH_BITS) != SCAN_2000_ALWAYS_HIGH_BITS) {
        return decodeDataError;
    }
    // Remove the bits, that are always high
    command &= ~SCAN_2000_ALWAYS_HIGH_BITS;
    *relaySetRegister = 0x0000;
    *relayUnsetRegister = 0x0000;

    // There is no need to run the loop, if there is nothing to do. Every second command
    // only contains the bits, that are always high. We can ignore those commands.
    if (command != 0x00000000) {
        // 10 channels + 1 4W relay
        for (uint8_t i = 0; i < 11; i++) {
            // Compile a list of channels, that are to be turned on
            // The scanner card supports up to 20 channels on two separate buses (CH1-CH10 and CH11-CH20),
            // but the DMM might only support 10 channels.
            // We therefore use channels CH1-CH5 and CH11-CH15 on the scanner card and skip CH6-CH10,
            // because CH1-CH5 and CH6-CH10 are connected to the same bus. This is why we use the
            // i + (i/5) * 5 term to skip CH6-CH10.
            // The MSB is the 4W relay, the LSB is CH1

            *relaySetRegister |= !!(command & (1 << scan2000ChannelOnSequence[i])) << (i + (i / 5) * 5);
            // The list of channels, that are to tbe turned off
            *relayUnsetRegister |= !!(command & (1 << scan2000ChannelOffSequence[i])) << (i + (i / 5) * 5);
        }
        rv = decodeOK;
    } else {
        rv = decodeIgnored;
    }
    // Always unset CH6-CH10 and CH-16-CH20
    // There is no need to not set the relaySetRegister, because unsetting a relay takes precidence.
    *relayUnsetRegister |= 0b11111000001111100000;

    return rv;
}

/**
 * @brief Decode 20 channel command
 * 48 bits in, so I decode all, even when I do not use all
 * @param command Command received
 * @param relaySetRegister ptr to bitmap for setting relays
 * @param relayUnsetRegister ptr to bitmap for unsetting relays
 * @return decodeResult_t
 */
decodeResult_t decode_20channels(uint64_t command, uint32_t *relaySetRegister, uint32_t *relayUnsetRegister) {
    *relaySetRegister = 0x0000;
    *relayUnsetRegister = 0x0000;

    // There is no need to run the loop, if there is nothing to do.
    if (command != 0x00000000) {
        // Process the channels (incl. CH21, 4W mode)
        for (uint8_t i = 0; i < SCAN2000_20_BITS; i++) {
          if ((command & ((uint64_t)1<<(2*i))) !=0) // Even clock pulses -> turn relays off
            *relayUnsetRegister |= ((uint32_t)1<<(scan2000_20ChannelSequence[i]-1));
          if ((command & ((uint64_t)1<<((2*i)+1))) !=0) // Odd clock pulses -> turn relays on
            *relaySetRegister |= ((uint32_t)1<<(scan2000_20ChannelSequence[i]-1));
        }
        return decodeOK;
    } else
        return decodeIgnored;
}

bool validateRelayState(uint32_t channelState) {
    // A valid state is the following:
    // - If the the two relay banks are connected, only one relay may be opened
    // - If the banks are disconnected (4W mode), one relay in each bank may be connected
    int countBank1 = __builtin_popcountl(channelState & CHANNELSTATE_BITMASK_BANK1);
    int countBank2 = __builtin_popcountl(channelState & CHANNELSTATE_BITMASK_BANK2);
    bool ch21Enabled = (CHANNELSTATE_BITMASK_4W & channelState);
    return
        (!ch21Enabled && (countBank1 + countBank2 <= 1))   // If "CH21" (4W Relay) is disabled
        || (ch21Enabled && (countBank1 <= 1 && countBank2 <= 1)); // If "CH21" (4W Relay) is enabled
}

void setRelays(uint32_t newChannelState) {
    // If we have a new state, update the relays
    if (newChannelState != channelState) {
        channelState = newChannelState;

        // First disconnect all channels, that need to be disconnected
        for (uint8_t i=0; i<NR_CHANNELS; i++) {
            if (!(channelState & (1 << i))) {
                HAL_GPIO_WritePin(GPIOsequence[i], PinSequence[i], GPIO_PIN_RESET);
            }
        }

        // If CH11-CH20 are turned off, disconnect them from the all buses to reduce the isolation capacitance
        // else connect it either to the 4W output or the sense output
        if (!(channelState & CHANNELSTATE_BITMASK_BANK2)) {
            HAL_GPIO_WritePin(Bus_Sense_GPIO_Port, Bus_Sense_Pin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(Bus_In_GPIO_Port, Bus_In_Pin, GPIO_PIN_RESET);
        } else {
            bool bit_4W = (channelState & CHANNELSTATE_BITMASK_4W);
            HAL_GPIO_WritePin(Bus_Sense_GPIO_Port, Bus_Sense_Pin, bit_4W);
            HAL_GPIO_WritePin(Bus_In_GPIO_Port, Bus_In_Pin, !bit_4W);
        }

        // Finally connect all channels, that need to be connected
        for (uint8_t i=0; i<NR_CHANNELS; i++) {
            if (channelState & (1 << i)) {
                HAL_GPIO_WritePin(GPIOsequence[i], PinSequence[i], GPIO_PIN_SET);
            }
        }
    }
}

void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin) {
#ifdef LOG_DEBUG_MESSAGES  
    // debug duration of the interrupt handler through the led line
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET); // led on
#endif

    if (GPIO_Pin == Clock_Pin) {
        // Clock in a new bit
        receivedSequence = receivedSequence << 1;
        receivedSequence |= HAL_GPIO_ReadPin(Data_GPIO_Port, Data_Pin);
        receivedCounter++;
        timeSinceLastClock = HAL_GetTick();
    } else if (GPIO_Pin == Strobe_Pin) {
#ifndef LOG_DEBUG_MESSAGES      
        // The sequence is over, decode it now
        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin); // toggle the led, every message
#endif        
        struct msgInfo msg;
        msg.state = msgUnknown;
        msg.timestamp = HAL_GetTick();
        msg.receivedCounter = receivedCounter;
        msg.receivedSequence = receivedSequence;
        msg.channelState = 0; // just a default value. Will be overwritten when possible.

        uint32_t newChannelState = channelState;
        uint32_t relaySetRegister = 0x00,  relayUnsetRegister = 0x00;
        decodeResult_t decodeResult = decodeLengthError; // error state by default

        if (receivedCounter == (BITS_FOR_SCAN2000_STD * 2)) {
            // We have a command for a 10 channel SCAN2000/SCAN2001 card
            decodeResult = decode_10channels((uint32_t)receivedSequence, &relaySetRegister, &relayUnsetRegister);
        } else if (receivedCounter == (SCAN2000_20_BITS * 2)) {
            decodeResult = decode_20channels((uint64_t)receivedSequence, &relaySetRegister, &relayUnsetRegister);
        } else {
            // Do not process the command, if it is of unknown size
            decodeResult = decodeLengthError;
        }

        if ((decodeResult == decodeOK) || (decodeResult == decodeIgnored)) {
            // Now apply the updates (can happen even when command is ignored, as I unset unwanted relays by default)
            newChannelState |= relaySetRegister;    // closed channels
            newChannelState &= ~relayUnsetRegister; // opened channels
            if (decodeResult == decodeOK)
                msg.state = msgOK;
            else
                msg.state = msgIgnored;

            // Test if the new state is valid and if so, apply it
            if (validateRelayState(newChannelState)) {
                setRelays(newChannelState);                  
            } else {
                msg.state = msgRelayError;
            }
        } else {
            // very likely decodeResult == decodeXError
            // Terminate here and signal an error
            if (decodeResult == decodeDataError)       
                msg.state = msgDataError;
            else
                msg.state = msgLengthError;
        }

        // signal the message to the print/log loop
        msg.channelState = newChannelState;
        MsgBuffer_add(msg);

        // and init for the next command
        receivedSequence = 0x00;
        receivedCounter = 0;
    }
#ifdef LOG_DEBUG_MESSAGES    
    // debug duration of the interrupt handler through the led line
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET); // led off
#endif    
}

int _write(int file, char *ptr, int len)
{
	int DataIdx;

	for (DataIdx = 0; DataIdx < len; DataIdx++)
	{
		__io_putchar(*ptr++);
	}
	return len;
}

/**
  * @brief  Retargets the C library printf function to the USART.
  * @param  None
  * @retval None
  */
PUTCHAR_PROTOTYPE
{
  /* Place your implementation of fputc here */
  /* e.g. write a character to the USART1 and Loop until the end of transmission */
  HAL_UART_Transmit(&huart4, (uint8_t *)&ch, 1, 0xFFFF);

  return ch;
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
