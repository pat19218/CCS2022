/*
 * adc_timer_uart.c
 * Programa que muestra el uso del ADC en la Tiva C. El muestreo se hace dentro
 * de la rutina de interrupci�n de un timer, para que sea uniforme. Las
 * conversiones se env�an por UART, para poder ser le�das en una computadora.
 *
 * Basado en los ejemplos single_ended.c y timers.c de TivaWare
 * Modificado por Luis Alberto Rivera
 */


#include <stdint.h>
#include <stdbool.h>
#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "driverlib/adc.h"
#include "driverlib/debug.h"
#include "driverlib/fpu.h"
#include "driverlib/gpio.h"
#include "driverlib/interrupt.h"
#include "driverlib/pin_map.h"
#include "driverlib/rom.h"
#include "driverlib/sysctl.h"
#include "driverlib/timer.h"
#include "driverlib/uart.h"
#include "utils/uartstdio.h"
#include "driverlib/ssi.h" //SPI

//*****************************************************************************
//Definiciones de la Configuraci�n SPI
//*****************************************************************************

#define NUM_SPI_DATA    1  // N�mero de palabras que se env�an cada vez
#define VALOR_MAX    4095  // Valor m�ximo del contador
#define SPI_FREC  4000000  // Frecuencia para el reloj del SPI
#define SPI_ANCHO      16  // N�mero de bits que se env�an cada vez, entre 4 y 16
#define MIN_DAC       0                // MIN_DAC = 0 porque va de 0 a 4095
#define COTA_INF      0                // Voltaje max esperado
#define COTA_SUP      3.3              // Voltaje max esperado


//*****************************************************************************
//                     Variables
//*****************************************************************************
float x1_n = 0.0;
float y1_n = 0.0;
float x1_n_1 = 0.0;
float y1_n_1 = 0.0;
float x1_n_2 = 0.0;
float y1_n_2 = 0.0;

float landa1 = 0.9;

uint8_t turno = 0;
uint8_t leo = 0;

uint32_t pui32residual[NUM_SPI_DATA];

//*****************************************************************************
//                     Prototipo de funcion
//*****************************************************************************
void InitConsole(void);
void Timer0IntHandler(void);
void InitSPI(void);
void delayMs(uint32_t ui32Ms);

//*****************************************************************************
//                  codigo principal
//*****************************************************************************

int main(void){


    uint16_t freq_muestreo = 1000;    // En Hz, 1/0.001

    //SysCtlClockSet(SYSCTL_SYSDIV_10 | SYSCTL_USE_PLL | SYSCTL_OSC_MAIN | SYSCTL_XTAL_16MHZ); // 20 MHz
    SysCtlClockSet(SYSCTL_SYSDIV_2_5 | SYSCTL_USE_PLL | SYSCTL_OSC_MAIN | SYSCTL_XTAL_16MHZ); // 80 MHz

    // Set up the serial console to use for displaying messages.  This is
    // just for this example program and is not needed for ADC operation.
    InitConsole();
    InitSPI();

    //-------------------ADC-----------------------------
    // The ADC0 peripheral must be enabled for use.
    SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);

    // For this example ADC0 is used with AIN0 on port E7.
    // The actual port and pins used may be different on your part, consult
    // the data sheet for more information.  GPIO port E needs to be enabled
    // so these pins can be used.
    // TODO: change this to whichever GPIO port you are using.
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);    //Activo puerto D
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);    //Activo puerto F

    // Select the analog ADC function for these pins.
    // Consult the data sheet to see which functions are allocated per pin.
    // TODO: change this to select the port/pin you are using.
    GPIOPinTypeADC(GPIO_PORTE_BASE, GPIO_PIN_3);
    GPIOPinTypeGPIOOutput(GPIO_PORTD_BASE, GPIO_PIN_2);
    GPIOPinTypeGPIOInput(GPIO_PORTF_BASE, GPIO_PIN_4);   //pines de entrada push

    //                  puerto,         pin,        amperaje,          Pull-up u otros          -->pg 265
    GPIOPadConfigSet(GPIO_PORTF_BASE, GPIO_PIN_4 , GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);   //Pull-up pin 4 puerto F (push1)


    // Enable sample sequence 3 with a processor signal trigger.  Sequence 3
    // will do a single sample when the processor sends a signal to start the
    // conversion.  Each ADC module has 4 programmable sequences, sequence 0
    // to sequence 3.  This example is arbitrarily using sequence 3.
    ADCSequenceConfigure(ADC0_BASE, 3, ADC_TRIGGER_PROCESSOR, 0);

    // Configure step 0 on sequence 3.  Sample channel 0 (ADC_CTL_CH0) in
    // single-ended mode (default) and configure the interrupt flag
    // (ADC_CTL_IE) to be set when the sample is done.  Tell the ADC logic
    // that this is the last conversion on sequence 3 (ADC_CTL_END). Sequence
    // 3 has only one programmable step.  Sequence 1 and 2 have 4 steps, and
    // sequence 0 has 8 programmable steps.  Since we are only doing a single
    // conversion using sequence 3 we will only configure step 0.  For more
    // information on the ADC sequences and steps, reference the datasheet.
    ADCSequenceStepConfigure(ADC0_BASE, 3, 0, ADC_CTL_CH0 | ADC_CTL_IE | ADC_CTL_END);

    // Since sample sequence 3 is now configured, it must be enabled.
    ADCSequenceEnable(ADC0_BASE, 3);

    // Clear the interrupt status flag.  This is done to make sure the
    // interrupt flag is cleared before we sample.
    ADCIntClear(ADC0_BASE, 3);

    //---------------------TRM0-----------------------------
    // Enable the peripherals used by this example.
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);

    // Enable processor interrupts.
    IntMasterEnable();

    // Configure the two 32-bit periodic timers.
    TimerConfigure(TIMER0_BASE, TIMER_CFG_PERIODIC);

    // El tercer argumento determina la frecuencia. El reloj se puede obtener
    // con SysCtlClockGet().
    // La frecuencia est� dada por SysCtlClockGet()/(valor del 3er argumento).
    // Ejemplos: Si se pone SysCtlClockGet(), la frecuencia ser� de 1 Hz.
    //           Si se pone SysCtlClockGet()/1000, la frecuencia ser� de 1 kHz
    //TimerLoadSet(TIMER0_BASE, TIMER_A, SysCtlClockGet());
    TimerLoadSet(TIMER0_BASE, TIMER_A, (uint32_t)(SysCtlClockGet()/freq_muestreo));

    // Setup the interrupt for the timer timeout.
    IntEnable(INT_TIMER0A);
    TimerIntEnable(TIMER0_BASE, TIMER_TIMA_TIMEOUT);

    // Enable the timers.
    TimerEnable(TIMER0_BASE, TIMER_A);

    // Las conversiones se hacen al darse la interrupci�n del timer, para que
    // el muestreo sea preciso. Luego de las configuraciones, el programa se
    // queda en un ciclo infinito haciendo nada.
    while(1){
        GPIOPinWrite(GPIO_PORTD_BASE, GPIO_PIN_2, 0x04); //0B00000100
        delayMs(100);  //delay de 100 ms para 10Hz
        GPIOPinWrite(GPIO_PORTD_BASE, GPIO_PIN_2, 0x00); //0B00000000
        delayMs(100);

        leo = GPIOPinRead(GPIO_PORTF_BASE, GPIO_PIN_4);

        if (leo == 0 && turno == 0){
            turno++;
        }else if(leo == 0 && turno == 1){
            turno = 0;
        }

    }
}



//*****************************************************************************
//
// The interrupt handler for the first timer interrupt.
//
//*****************************************************************************
void Timer0IntHandler(void) {
    // This array is used for storing the data read from the ADC FIFO. It
    // must be as large as the FIFO for the sequencer in use.  This example
    // uses sequence 3 which has a FIFO depth of 1.  If another sequence
    // was used with a deeper FIFO, then the array size must be changed.
    uint32_t pui32ADC0Value[1];

    // Clear the timer interrupt. Necesario para lanzar la pr�xima interrupci�n.
    TimerIntClear(TIMER0_BASE, TIMER_TIMA_TIMEOUT);

    // Trigger the ADC conversion.
    ADCProcessorTrigger(ADC0_BASE, 3);

    // Wait for conversion to be completed.
    while(!ADCIntStatus(ADC0_BASE, 3, false))
    {
    }

    // Clear the ADC interrupt flag.
    ADCIntClear(ADC0_BASE, 3);

    // Read ADC Value.
    ADCSequenceDataGet(ADC0_BASE, 3, pui32ADC0Value);
    x1_n = pui32ADC0Value[0];

    //Suavizador 1
    if(turno==1){
        y1_n = ((1-landa1) * x1_n) + (landa1 * y1_n_1);
    }else{
        y1_n = (landa1 * x1_n) - (landa1 * x1_n_1) +  (landa1 * y1_n_1);
    }

    y1_n_1 = y1_n;
    y1_n_2 = y1_n_2;
    x1_n_1 = x1_n;
    x1_n_2 = x1_n_1;


    // Display the AIN0 (PE3) digital value on the console.
    UARTprintf("%d\n", (int) x1_n);
    UARTprintf(",");
    UARTprintf("%d\n", (int) y1_n);
}

void InitConsole(void) {
    // Enable GPIO port A which is used for UART0 pins.
    // TODO: change this to whichever GPIO port you are using.
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);

    // Configure the pin muxing for UART0 functions on port A0 and A1.
    // This step is not necessary if your part does not support pin muxing.
    // TODO: change this to select the port/pin you are using.
    GPIOPinConfigure(GPIO_PA0_U0RX);
    GPIOPinConfigure(GPIO_PA1_U0TX);

    // Enable UART0 so that we can configure the clock.
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);

    // Use the internal 16MHz oscillator as the UART clock source.
    UARTClockSourceSet(UART0_BASE, UART_CLOCK_PIOSC);

    // Select the alternate (UART) function for these pins.
    // TODO: change this to select the port/pin you are using.
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);

    // Initialize the UART for console I/O.
    UARTStdioConfig(0, 230400, 16000000);
    //UARTStdioConfig(0, 115200, 16000000);
}
void delayMs(uint32_t ui32Ms) {

    // 1 clock cycle = 1 / SysCtlClockGet() second
    // 1 SysCtlDelay = 3 clock cycle = 3 / SysCtlClockGet() second
    // 1 second = SysCtlClockGet() / 3
    // 0.001 second = 1 ms = SysCtlClockGet() / 3 / 1000

    SysCtlDelay(ui32Ms * (SysCtlClockGet() / 3 / 1000));
}

void InitSPI(void){
    SysCtlPeripheralEnable(SYSCTL_PERIPH_SSI0);     // Habilitar el periferico del SSIO
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);    // Habilitar los perifericos de los pines
    GPIOPinConfigure(GPIO_PA2_SSI0CLK);             // Configure the pin muxing
    GPIOPinConfigure(GPIO_PA3_SSI0FSS);
    GPIOPinConfigure(GPIO_PA4_SSI0RX);
    GPIOPinConfigure(GPIO_PA5_SSI0TX);
    GPIOPinTypeSSI(GPIO_PORTA_BASE, GPIO_PIN_5 | GPIO_PIN_4 | GPIO_PIN_3 | GPIO_PIN_2);                           // Configurar los pines para el SSI
    SSIConfigSetExpClk(SSI0_BASE, SysCtlClockGet(), SSI_FRF_MOTO_MODE_0, SSI_MODE_MASTER, SPI_FREC, SPI_ANCHO);   // Configurar los puertos del SSI para el modo master del SPI
    SSIEnable(SSI0_BASE);                                               // Enable the SSI0 module
    // Read any residual data from the SSI port.  This makes sure the receive
    // FIFOs are empty, so we don't read any unwanted junk.  This is done here
    // because the SPI SSI mode is full-duplex, which allows you to send and
    // receive at the same time.  The SSIDataGetNonBlocking function returns
    // "true" when data was returned, and "false" when no data was returned.
    // The "non-blocking" function checks if there is any data in the receive
    // FIFO and does not "hang" if there isn't.
    while(SSIDataGetNonBlocking(SSI0_BASE, &pui32residual[0])){
    }
}
