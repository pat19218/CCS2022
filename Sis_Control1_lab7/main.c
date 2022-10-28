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
//spi
#include "driverlib/ssi.h"
//*****************************************************************************
//
//! \addtogroup adc_examples_list
//! <h1>Single Ended ADC (single_ended)</h1>
//!
//! Este ejemplo muestra cómo se puede muestrear en dos canales ADC de forma
//! simultánea. Se usan los puertos AIN0/PE3 y AIN1/PE2.
//!
//! This example uses the following peripherals and I/O signals.  You must
//! review these and change as needed for your own board:
//! - ADC0 peripheral
//! - GPIO Port E peripheral (for AIN0 and AIN1 pin)
//! - AIN0 - PE3
//! - AIN1 - PE2
//!
//! The following UART signals are configured only for displaying console
//! messages for this example.  These are not required for operation of the
//! ADC.
//! - UART0 peripheral
//! - GPIO Port A peripheral (for UART0 pins)
//! - UART0RX - PA0
//! - UART0TX - PA1
//!
//! This example uses the following interrupt handlers.  To use this example
//! in your own application you must add these interrupt handlers to your
//! vector table.
//! - Timer0IntHandler.
//
//*****************************************************************************
//Definiciones de la Configuracion SPI
#define NUM_SPI_DATA    1  // Numero de palabras que se envian cada vez
#define VALOR_MAX    4095  // Valor maximo del contador
#define SPI_FREC  4000000  // Frecuencia para el reloj del SPI
#define SPI_ANCHO      16  // Número de bits que se envían cada vez, entre 4 y 16
#define MIN_DAC       0                // MIN_DAC = 0 porque va de 0 a 4095
#define COTA_INF      0                // Voltaje max esperado
#define COTA_SUP      3.3              // Voltaje max esperado
#define FREQ_TIMER 1000
//Variables
uint16_t dato_in =0;
uint16_t dato_out =0;
uint16_t contador =0;

float referencia, salida;
float e_k, e_k_1 = 0.0,E_k = 0.0, ed, u_k;
float kP=3.9774, kI=233.8472, kD=0.015029;//3.9774


//*****************************************************************************
//
// The interrupt handler for the first timer interrupt.
//
//*****************************************************************************
void
Timer0IntHandler(void)
{
    // This array is used for storing the data read from the ADC FIFO. It
    // must be as large as the FIFO for the sequencer in use.  This example
    // uses sequence 3 which has a FIFO depth of 1.  If another sequence
    // was used with a deeper FIFO, then the array size must be changed.
    uint32_t pui32ADC0Value[2];

    uint32_t pui32DataTx[NUM_SPI_DATA]; // la función put pide tipo uint32_t
    uint8_t ui32Index;

    // Clear the timer interrupt. Necesario para lanzar la próxima interrupción.
    TimerIntClear(TIMER0_BASE, TIMER_TIMA_TIMEOUT);

    // Trigger the ADC conversion.
    ADCProcessorTrigger(ADC0_BASE, 2);  // Notar el cambio de "secuencia" (2 en lugar de 3).


    // Wait for conversion to be completed.
    while(!ADCIntStatus(ADC0_BASE, 2, false))//pin 29
    {
    }

    // Clear the ADC interrupt flag.
    ADCIntClear(ADC0_BASE, 2);

    // Ahora se leen dos valores, no sólo uno, según la configuración.
    // En pui32ADC0Value[0] se tendrá el valor del puerto AIN0 y
    // en pui32ADC0Value[1] se tendrá el valor del puerto AIN1, porque así se
    // configuró en el main.
    ADCSequenceDataGet(ADC0_BASE, 2, pui32ADC0Value);  // Notar el cambio de "secuencia".

    //Interrupcion
    referencia = pui32ADC0Value[0] *3.3/4095.0;//convertir a voltios
    salida = pui32ADC0Value[1] *3.3/4095.0; //convertir a voltios

    e_k = referencia - salida;
    ed = e_k - e_k_1;
    E_k = E_k + e_k;
    u_k = kP*e_k + (kI/FREQ_TIMER)*E_k + (kD*FREQ_TIMER)*ed;
    e_k_1 = e_k;

    if (u_k > COTA_SUP) {
        u_k = COTA_SUP;
    }
    if (u_k < COTA_INF) {
        u_k = COTA_INF;
    }

    dato_out = (uint16_t) (u_k*4095.0/3.3);

    // Display the AIN0 (PE3) digital value on the console.


    pui32DataTx[0] = (uint32_t)((0b0111 << 12) | (0x0FFF & (int)dato_out));//dato a enviar
    // Send data
    for(ui32Index = 0; ui32Index < NUM_SPI_DATA; ui32Index++)
    {
            // Send the data using the "blocking" put function.  This function
            // will wait until there is room in the send FIFO before returning.
            // This allows you to assure that all the data you send makes it into
            // the send FIFO.
        SSIDataPut(SSI0_BASE, pui32DataTx[ui32Index]);
    }
        // Wait until SSI0 is done transferring all the data in the transmit FIFO.
    while(SSIBusy(SSI0_BASE))
    {
    }
    contador++;
    if (contador > 4095)
            contador =0;
    //UARTprintf("%d\n", (int)data_out);
    // Se muestra el valor convertido de AIN0 (PE3) y AIN1 (PE2), que son enteros
    // entre 0 y 4095, y los valores en voltios, entre 0.00 y 3.30.
    // Nota: UARTprintf no soporta floats (%f). Acá se hace un truco para mostrar
    // la parte entera y dos posiciones decimales, como enteros.
    // Notar que el "casting" trunca, no redondea.
    // Si no es indispensable desplegar floats, mejor evitar hacerlo.
/*    UARTprintf("A0: %04d, v0: %d.%02d,   A1: %04d, v1: %d.%02d\n",
               pui32ADC0Value[0], (uint8_t)v0, (uint8_t)(100*(v0-(uint8_t)v0)),
               pui32ADC0Value[1], (uint8_t)v1, (uint8_t)(100*(v1-(uint8_t)v1)));
*/

}


//*****************************************************************************
//
// This function sets up UART0 to be used for a console to display information
// as the example is running.
//
//*****************************************************************************
void
InitConsole(void)
{
    // Enable GPIO port A which is used for UART0 pins.
    // all: change this to whichever GPIO port you are using.
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);

    // Configure the pin muxing for UART0 functions on port A0 and A1.
    // This step is not necessary if your part does not support pin muxing.
    // all: change this to select the port/pin you are using.
    GPIOPinConfigure(GPIO_PA0_U0RX);
    GPIOPinConfigure(GPIO_PA1_U0TX);

    // Enable UART0 so that we can configure the clock.
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);

    // Use the internal 16MHz oscillator as the UART clock source.
    UARTClockSourceSet(UART0_BASE, UART_CLOCK_PIOSC);

    // Select the alternate (UART) function for these pins.
    // all: change this to select the port/pin you are using.
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);

    // Initialize the UART for console I/O.
    UARTStdioConfig(0, 115200, 16000000);
}
//*****************************************************************************
//
// Configure ADC0 for a single-ended input and a single sample.  Once the
// sample is ready, an interrupt flag will be set.  Using a polling method,
// the data will be read then displayed on the console via UART0.
//
//*****************************************************************************
int
main(void)
{
    uint32_t pui32residual[NUM_SPI_DATA];
    uint16_t freq_muestreo = FREQ_TIMER;    // En Hz
    // Set the clocking to run at 80 MHz (200 MHz / 2.5) using the PLL.  When
    // using the ADC, you must either use the PLL or supply a 16 MHz clock source.
    // all: The SYSCTL_XTAL_ value must be changed to match the value of the
    // crystal on your board.
    //SysCtlClockSet(SYSCTL_SYSDIV_10 | SYSCTL_USE_PLL | SYSCTL_OSC_MAIN |
    //                 SYSCTL_XTAL_16MHZ); // 20 MHz
    SysCtlClockSet(SYSCTL_SYSDIV_2_5 | SYSCTL_USE_PLL | SYSCTL_OSC_MAIN |
                   SYSCTL_XTAL_16MHZ); // 80 MHz
    // Set up the serial console to use for displaying messages.  This is
    // just for this example program and is not needed for ADC operation.
    InitConsole();


    //*************SPI**************
    SysCtlPeripheralEnable(SYSCTL_PERIPH_SSI0);                         // Habilitar el periferico del SSIO
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);                        // Habilitar los perifericos de los pines
    GPIOPinConfigure(GPIO_PA2_SSI0CLK);                                 // Configure the pin muxing
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
    while(SSIDataGetNonBlocking(SSI0_BASE, &pui32residual[0]))
    {
    }

    //***************Configuración*********Puertos******Pines************
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);    //PuertoB
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOB)) //Check if the peripheral acces is enabled
    {
    }
    while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF))
    {
    }
    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_1);
    GPIOPinTypeGPIOOutput(GPIO_PORTB_BASE, GPIO_PIN_1); //PIN PARA SEÑAL CUADRADA


    //***********Configuración ADC*************
    // The ADC0 peripheral must be enabled for use.
    //ConfiguracionPuertos
    SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
    // For this example ADC0 is used with AIN0 on port E7.
    // The actual port and pins used may be different on your part, consult
    // the data sheet for more information.  GPIO port E needs to be enabled
    // so these pins can be used.
    // all: change this to whichever GPIO port you are using.
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
    // Select the analog ADC function for these pins.
    // Consult the data sheet to see which functions are allocated per pin.
    // all: change this to select the port/pin you are using.
    GPIOPinTypeADC(GPIO_PORTE_BASE, GPIO_PIN_3);  // Configura el pin PE3
    GPIOPinTypeADC(GPIO_PORTE_BASE, GPIO_PIN_2);  // Configura el pin PE2
    // Enable sample sequence 3 with a processor signal trigger.  Sequence 3
    // will do a single sample when the processor sends a signal to start the
    // conversion.  Each ADC module has 4 programmable sequences, sequence 0
    // to sequence 3.  This example is arbitrarily using sequence 3.
    ADCSequenceConfigure(ADC0_BASE, 2, ADC_TRIGGER_PROCESSOR, 0);
    // Configure step 0 on sequence 3.  Sample channel 0 (ADC_CTL_CH0) in
    // single-ended mode (default) and configure the interrupt flag
    // (ADC_CTL_IE) to be set when the sample is done.  Tell the ADC logic
    // that this is the last conversion on sequence 3 (ADC_CTL_END). Sequence
    // 3 has only one programmable step.  Sequence 1 and 2 have 4 steps, and
    // sequence 0 has 8 programmable steps.  Since we are only doing a single
    // conversion using sequence 3 we will only configure step 0.  For more
    // information on the ADC sequences and steps, reference the datasheet.
    // Step 0 en la secuencia 2: Canal 0 (ADC_CTL_CH0) en modo single-ended (por defecto).
    ADCSequenceStepConfigure(ADC0_BASE, 2, 0, ADC_CTL_CH0);
    // Step 1 en la secuencia 2: Canal 1 (ADC_CTL_CH1) en modo single-ended (por defecto),
    // y configura la bandera de interrupción (ADC_CTL_IE) para "setearse"
    // cuando se tenga esta muestra. También se indica que esta es la última
    // conversión en la secuencia 2 (ADC_CTL_END).
    // Para más detalles del módulo ADC, consultar el datasheet.
    ADCSequenceStepConfigure(ADC0_BASE, 2, 1, ADC_CTL_CH1 | ADC_CTL_IE | ADC_CTL_END);

    // Since sample sequence 3 is now configured, it must be enabled.
    // Since sample sequence 2 is now configured, it must be enabled.
    ADCSequenceEnable(ADC0_BASE, 2);
    // Clear the interrupt status flag.  This is done to make sure the
    // interrupt flag is cleared before we sample.
    ADCIntClear(ADC0_BASE, 2);


    //****TIMER0********************
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);   //Habilita perifericos
    IntMasterEnable();                              //Habilita interrupciones.
    TimerConfigure(TIMER0_BASE, TIMER_CFG_PERIODIC);//Configurar los dos temporizadores periódicos de 32 bits
    // El tercer argumento determina la frecuencia. El reloj se puede obtener
    // con SysCtlClockGet().
    // La frecuencia está dada por SysCtlClockGet()/(valor del 3er argumento).
    // Ejemplos: Si se pone SysCtlClockGet(), la frecuencia será de 1 Hz.
    //           Si se pone SysCtlClockGet()/1000, la frecuencia será de 1 kHz
    //TimerLoadSet(TIMER0_BASE, TIMER_A, SysCtlClockGet());
    TimerLoadSet(TIMER0_BASE, TIMER_A, (uint32_t)(SysCtlClockGet()/freq_muestreo));
    IntEnable(INT_TIMER0A);                         // Setup the interrupt for the timer timeout.
    TimerIntEnable(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
    TimerEnable(TIMER0_BASE, TIMER_A); //Habilitar los Timers
    // Las conversiones se hacen al darse la interrupción del timer, para que
    // el muestreo sea preciso. Luego de las configuraciones, el programa se
    // queda en un ciclo infinito haciendo nada.
    while(1)
    {
        /*SysCtlDelay(50*SysCtlClockGet() / (3*1000));
        GPIOPinWrite(GPIO_PORTB_BASE, GPIO_PIN_1, GPIO_PIN_1);
        GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1, GPIO_PIN_1);
        SysCtlDelay(50*SysCtlClockGet() / (3*1000));
        GPIOPinWrite(GPIO_PORTB_BASE, GPIO_PIN_1, 0x0);
        GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1, 0x0);
        */
    }
}

