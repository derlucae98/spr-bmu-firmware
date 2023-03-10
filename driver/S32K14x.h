/*
 * S32K14x.h
 *
 *  Created on: Mar 10, 2023
 *      Author: luca
 */

#ifndef S32K14X_H_
#define S32K14X_H_

    #if defined(CPU_S32K142)
        #include <S32K142.h>
    #elif defined(CPU_S32K144)
        #include <S32K144.h>
    #elif defined(CPU_S32K146)
        #include <S32K146.h>
    #elif defined(CPU_S32K148)
        #include <S32K148.h>
    #endif
#endif /* S32K14X_H_ */
