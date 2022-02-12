/*
 * config.h
 *
 *  Created on: Feb 12, 2022
 *      Author: scuderia
 */

#ifndef CONFIG_H_
#define CONFIG_H_

#define NUMBEROFSLAVES 1
#define CELL_UNDERVOLTAGE 2700
#define CELL_OVERVOLTAGE  4200
#define LTC6811_SPI LPSPI2

#define MAXSTACKS 12
#define MAXCELLS  12
#define MAXTEMPSENS 14
#define MAXCELLTEMP 540 //54.0 deg C

#endif /* CONFIG_H_ */
