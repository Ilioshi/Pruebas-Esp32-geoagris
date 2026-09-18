/**
 * @file Protocol_utils.h
 * @author Mariano Oms (omsmarian@gmail.com)
 * @brief For definitions and macros shared among CAN protocols  
 * @version 0.1
 * @date 2026-02-03
 * 
 */

#ifndef PROTOCOL_UTILS_H
#define PROTOCOL_UTILS_H

/************************************************************************************
*                                     Defines                                       *
*************************************************************************************/
// valid bits in CAN ID for frame formats
#define CAN_EFF_MASK                0x1FFFFFFFUL    // Extended Frame Format (29 bits)

// PGN definitions for ISOBUS
#define PGN_VEHICLE_POSITION_1      0xFEF3
#define PGN_DATE_TIME               0xFEE6
#define PGN_SPEED_DIRECTION         0xFEE8
#define PGN_PROCESS_DATA_MESSAGE    0xCB00
#define PGN_


// DDI definitions for ISOBUS
#define DDI_SETPOINT_VOLUME_PER_AREA    0x0001
#define DDI_ACTUAL_VOLUME_PER_AREA      0x0002
#define DDI_SETPOINT_MASS_PER_AREA      0x0006
#define DDI_ACTUAL_MASS_PER_AREA        0x0007
#define DDI_ACTUAL_WORK_STATE           0x008D
#define DDI_ACTUAL_PRODUCT_PRESSURE     0x00C2
#define DDI_ACTUAL_CONDENSED_WORK_STATE 0x00A1
#define DDI_WIND_SPEED                  0x00CF
#define DDI_WIND_DIRECTION              0x00D0
#define DDI_HUMIDITY                    0x00D1
#define DDI_TEMPERATURE                 0x00C0

/************************************************************************************
*                                     Macros                                        *
*************************************************************************************/

#define READ_U16_LE(dataPtr) ((uint16_t)(dataPtr)[0] | ((uint16_t)(dataPtr)[1] << 8))

// For more info read the following links:
// https://www.csselectronics.com/pages/isobus-introduction-tutorial-iso-11783
// https://www.csselectronics.com/pages/j1939-explained-simple-intro-tutorial
// https://www.csselectronics.com/pages/can-bus-simple-intro-tutorial

#define CAN_FRAME_PF(canId)   ((((uint32_t)(canId) & CAN_EFF_MASK) >> 16) & 0xFF) 
#define CAN_FRAME_PGN(canId)  ((((uint32_t)(canId) & CAN_EFF_MASK) >> 8) & 0x3FFFF)
#define EXTRACT_PGN(canId)    ((CAN_FRAME_PF(canId) < 240) ? (CAN_FRAME_PGN(canId) & 0x3FF00) : CAN_FRAME_PGN(canId))

// DDI reading macro
#define READ_DDI(dataPtr)    READ_U16_LE(dataPtr)


#endif // PROTOCOL_UTILS_H