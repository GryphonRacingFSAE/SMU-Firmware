#include "ThermistorMonitor.h"
#include "utils.h"
#include "main.h"
#include <math.h>
#include <CAN.h>

extern ADC_HandleTypeDef hadc1;


// Use NTC 10K Thermistor Parameters
// Values found here: https://www.vishay.com/docs/29218/ntcle350e4.pdf

#define CALIBRATION_TEMPERATURE 298.15
#define CALIBRATION_RESISTANCE 10000
#define THERMISTOR_BETA 3435

#define DIVIDER_RESISTANCE 10000
#define ADC_RESOLUTION 4095
#define REFERENCE_VOLTAGE 3.3

// Use Steinhart & Hart coefficients for NTC 10k Thermistor
// Values found using this site (ordering code: NTCLE350E4103FLB0): https://www.vishay.com/en/thermistors/ntc-rt-calculator/
// (VALUES ARE HIDDEN UNDER INSPECT ELEMENT)
#define TEMP_COEFF_A 3.354016434680530000E-3
#define TEMP_COEFF_B 3.00130825115663000E-4
#define TEMP_COEFF_C 5.085164943790940E-6
#define TEMP_COEFF_D 2.18765049258341E-7

//#define RES_COEFF_A -12.89228328
//#define RES_COEFF_B 4245.14800000
//#define RES_COEFF_C -87493.00000000
//#define RES_COEFF_D -9588114.00000000



ThermistorData_Struct ThermistorData = { .thermistors = {} };

void adcChangeMUX(int channel) {
	ADC_ChannelConfTypeDef sConfig = {0};

	// Selects new ADC channel for conversion
	if (DEV_BOARD == 0) {
		switch(channel) {

		case 0:
			sConfig.Channel = ADC_CHANNEL_6;
			break;
		case 1:
			sConfig.Channel = ADC_CHANNEL_7;
			break;
		case 2:
			sConfig.Channel = ADC_CHANNEL_8;
			break;
		case 3:
			sConfig.Channel = ADC_CHANNEL_9;
			break;
		}
	} else {
		sConfig.Channel = ADC_CHANNEL_8;
	}

	sConfig.Rank = 1;
	sConfig.SamplingTime = ADC_SAMPLETIME_28CYCLES_5;
	if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
		Error_Handler();
	}
}

void startThermistorMonitorTask() {
	uint32_t tick = osKernelGetTickCount();
	uint8_t select_line = 0;



	while (1) {

		// Resetting pins A, B, C; then setting the next binary sequence as per table 1:
		HAL_GPIO_WritePin(GPIOA, 0b111000, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(GPIOA, select_line<<3, GPIO_PIN_SET);

		osDelay(100);

        // Grab the temperature for all modules at once
        for (uint8_t cur_mux = 0; cur_mux < MUX_COUNT; cur_mux++) {
        	// changes the MUX that the ADC polls
        	adcChangeMUX(cur_mux);
            HAL_ADC_Start(&hadc1);
            HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
            uint32_t divider_output = HAL_ADC_GetValue(&hadc1);
            HAL_ADC_Stop(&hadc1);


            DEBUG_PRINT("----------------------------\n");
            DEBUG_PRINT("Divider ADC out: %d\n", divider_output);


            // converts ADC units to volts
            float divider_voltage = ((float) divider_output/(float) ADC_RESOLUTION * REFERENCE_VOLTAGE);


            DEBUG_PRINT("Divider Voltage: %d.%d\n", (int)divider_voltage, (int) (divider_voltage * 1000) % 1000);

            // calculates resistance of the thermistor using rearrangement of voltage divider formula: https://ohmslawcalculator.com/voltage-divider-calculator
            float thermistor_resistance = (divider_voltage * DIVIDER_RESISTANCE) / (REFERENCE_VOLTAGE - divider_voltage);

            DEBUG_PRINT("Thermistor Resistance: %d\n", (int) (thermistor_resistance));


		   // TODO: pick a better sentinel value
            if (thermistor_resistance == 0) {
            	ThermistorData.thermistors[cur_mux][select_line] = -1000;
            	continue;
            }

			// Convert Thermistor Resistance to Temperature
			// This is a rearrangement of the equation found here: https://www.lasercalculator.com/ntc-thermistor-calculator/
			float therm_temp = (float) (CALIBRATION_TEMPERATURE * THERMISTOR_BETA) / ((CALIBRATION_TEMPERATURE*log(thermistor_resistance/CALIBRATION_RESISTANCE)) + THERMISTOR_BETA);
			DEBUG_PRINT("Thermistor Temperature (NTC appoximation)(K): %d.%d\n", (int) (therm_temp), (int) ((int) (therm_temp * 100) % 100));

			// This is an equation which is can be more accurate than generalizing the shape of the temperature-resistange curve, found here: https://www.ametherm.com/thermistor/ntc-thermistors-steinhart-and-hart-equation
			float stein_temp = 1.0 / (TEMP_COEFF_A + (TEMP_COEFF_B * log(thermistor_resistance/(float) CALIBRATION_RESISTANCE)) + TEMP_COEFF_C * powf(log(thermistor_resistance/(float) CALIBRATION_RESISTANCE), 2.0) + TEMP_COEFF_D * powf(log(thermistor_resistance/(float) CALIBRATION_RESISTANCE), 3.0));
			DEBUG_PRINT("Thermistor Temperature (Steinhart & Hart Coeff) (K): %d.%d\n", (int) (stein_temp), (int) ((int) (stein_temp * 100) % 100));

//			float stein_resistance = CALIBRATION_RESISTANCE * exp(RES_COEFF_A + RES_COEFF_B/stein_temp + RES_COEFF_C/powf(stein_temp, 2.0) + RES_COEFF_D/powf(stein_temp, 3.0));
//			DEBUG_PRINT("Thermistor Accurate Resistance (Stein): %d.%d", (int)((stein_resistance*100)/100), (int)((int) (stein_resistance*100)%100));

			// kelvin to celsius conversion
            float temp_celsius = therm_temp - 273.15f;
            float stein_temp_celsius = stein_temp - 273.15f;

            DEBUG_PRINT("Thermistor Temperature (C): %d.%d\n", (int) (temp_celsius), abs((int) ((int) (temp_celsius * 100) % 100)));
            DEBUG_PRINT("Thermistor Temperature (SteinHart & Hart) (C): %d.%d\n", (int) (stein_temp_celsius), abs((int) ((int) (stein_temp_celsius * 100) % 100)));
            DEBUG_PRINT("Thermistor channel: %d\n", select_line);
            DEBUG_PRINT("Current MUX: %d\n", cur_mux);

            //GRCprintf("Channel: %d, Count: %d, Temp: %d, Resistance: %d, Percent of voltage: %d, Measured Voltage: %d\n", (int32_t)cur_mux, (int32_t)select_line, thermistor_temperature, (int32_t)thermistor_resistance, (int32_t)(percent_of_divider_input_voltage * 100), divider_voltage);

            ThermistorData.thermistors[cur_mux][select_line] = therm_temp;
        }

		//Counter Variable for MUX Select Lines
        if (DEV_BOARD == 0) {
			if (++select_line == THERMISTORS_PER_MUX) {
				select_line = 0;
			}
        } else {
			if (select_line == 0) {
				select_line = 3;
			} else {
				select_line = 0;
			}
        }

        CANTXMsg bms_broadcast = {
        	.header = {
        		.IDE = CAN_ID_EXT,
        		.RTR = CAN_RTR_DATA,
        		.ExtId = 0x1839F380,
        		.DLC = 8
        	},
        	.to = &hcan
        };



        osMessageQueuePut(CANTX_QHandle, &bms_broadcast, 0, 5);
        //GRCprintf("Sent TX broadcast\n");

		osDelayUntil(tick += THERMISTOR_PERIOD);
	}
}
