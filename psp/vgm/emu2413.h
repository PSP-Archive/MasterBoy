#ifndef _EMU2413_H_
#define _EMU2413_H_

#include "emutypes.h"

#ifdef EMU2413_DLL_EXPORTS
  #define EMU2413_API __declspec(dllexport)
#elif defined(EMU2413_DLL_IMPORTS)
  #define EMU2413_API __declspec(dllimport)
#else
  #define EMU2413_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define PI 3.14159265358979323846

enum EMU2413_OPLL_TONE_ENUM {EMU2413_OPLL_2413_TONE=0, EMU2413_OPLL_VRC7_TONE=1, EMU2413_OPLL_281B_TONE=2} ;

/* voice data */
typedef struct __EMU2413_OPLL_PATCH {
  e_uint32 TL,FB,EG,ML,AR,DR,SL,RR,KR,KL,AM,PM,WF ;
} EMU2413_OPLL_PATCH ;

/* slot */
typedef struct __EMU2413_OPLL_SLOT {

  EMU2413_OPLL_PATCH *patch;  

  e_int32 type ;          /* 0 : modulator 1 : carrier */

  /* OUTPUT */
  e_int32 feedback ;
  e_int32 output[2] ;   /* Output value of slot */

  /* for Phase Generator (PG) */
  e_uint16 *sintbl ;    /* Wavetable */
  e_uint32 phase ;      /* Phase */
  e_uint32 dphase ;     /* Phase increment amount */
  e_uint32 pgout ;      /* output */

  /* for Envelope Generator (EG) */
  e_int32 fnum ;          /* F-Number */
  e_int32 block ;         /* Block */
  e_int32 volume ;        /* Current volume */
  e_int32 sustine ;       /* Sustine 1 = ON, 0 = OFF */
  e_uint32 tll ;	      /* Total Level + Key scale level*/
  e_uint32 rks ;        /* Key scale offset (Rks) */
  e_int32 eg_mode ;       /* Current state */
  e_uint32 eg_phase ;   /* Phase */
  e_uint32 eg_dphase ;  /* Phase increment amount */
  e_uint32 egout ;      /* output */

} EMU2413_OPLL_SLOT ;

/* Mask */
#define EMU2413_OPLL_MASK_CH(x) (1<<(x))
#define EMU2413_OPLL_MASK_HH (1<<(9))
#define EMU2413_OPLL_MASK_CYM (1<<(10))
#define EMU2413_OPLL_MASK_TOM (1<<(11))
#define EMU2413_OPLL_MASK_SD (1<<(12))
#define EMU2413_OPLL_MASK_BD (1<<(13))
#define EMU2413_OPLL_MASK_RHYTHM ( EMU2413_OPLL_MASK_HH | EMU2413_OPLL_MASK_CYM | EMU2413_OPLL_MASK_TOM | EMU2413_OPLL_MASK_SD | EMU2413_OPLL_MASK_BD )

/* opll */
typedef struct __OPLL {

  e_uint32 adr ;
  e_int32 out ;

#ifndef EMU2413_COMPACTION
  e_uint32 realstep ;
  e_uint32 oplltime ;
  e_uint32 opllstep ;
  e_int32 prev, next ;
  e_int32 sprev[2],snext[2];
  e_int32 pan[16];
#endif

  /* Register */
  e_uint8 reg[0x40] ; 
  e_int32 slot_on_flag[18] ;

  /* Pitch Modulator */
  e_uint32 pm_phase ;
  e_int32 lfo_pm ;

  /* Amp Modulator */
  e_int32 am_phase ;
  e_int32 lfo_am ;

  e_uint32 quality;

  /* Noise Generator */
  e_uint32 noise_seed ;

  /* Channel Data */
  e_int32 patch_number[9];
  e_int32 key_status[9] ;

  /* Slot */
  EMU2413_OPLL_SLOT slot[18] ;

  /* Voice Data */
  EMU2413_OPLL_PATCH patch[19*2] ;
  e_int32 patch_update[2] ; /* flag for check patch update */

  e_uint32 mask ;

} OPLL ;

/* Create Object */
EMU2413_API OPLL *EMU2413_OPLL_new(e_uint32 clk, e_uint32 rate) ;
EMU2413_API void EMU2413_OPLL_delete(OPLL *) ;

/* Setup */
EMU2413_API void EMU2413_OPLL_reset(OPLL *) ;
EMU2413_API void EMU2413_OPLL_reset_patch(OPLL *, e_int32) ;
EMU2413_API void EMU2413_OPLL_set_rate(OPLL *opll, e_uint32 r) ;
EMU2413_API void EMU2413_OPLL_set_quality(OPLL *opll, e_uint32 q) ;
EMU2413_API void EMU2413_OPLL_set_pan(OPLL *, e_uint32 ch, e_uint32 pan);

/* Port/Register access */
EMU2413_API void EMU2413_OPLL_writeIO(OPLL *, e_uint32 reg, e_uint32 val) ;
EMU2413_API void EMU2413_OPLL_writeReg(OPLL *, e_uint32 reg, e_uint32 val) ;

/* Synthsize */
EMU2413_API e_int16 EMU2413_OPLL_calc(OPLL *) ;
EMU2413_API void EMU2413_OPLL_calc_stereo(OPLL *, e_int32 out[2]) ;

/* Misc */
EMU2413_API void EMU2413_OPLL_setPatch(OPLL *, const e_uint8 *dump) ;
EMU2413_API void EMU2413_OPLL_copyPatch(OPLL *, e_int32, EMU2413_OPLL_PATCH *) ;
EMU2413_API void EMU2413_OPLL_forceRefresh(OPLL *) ;
/* Utility */
EMU2413_API void EMU2413_OPLL_dump2patch(const e_uint8 *dump, EMU2413_OPLL_PATCH *patch) ;
EMU2413_API void EMU2413_OPLL_patch2dump(const EMU2413_OPLL_PATCH *patch, e_uint8 *dump) ;
EMU2413_API void EMU2413_OPLL_getDefaultPatch(e_int32 type, e_int32 num, EMU2413_OPLL_PATCH *) ;

/* Channel Mask */
EMU2413_API e_uint32 EMU2413_OPLL_setMask(OPLL *, e_uint32 mask) ;
EMU2413_API e_uint32 EMU2413_OPLL_toggleMask(OPLL *, e_uint32 mask) ;

#define dump2patch EMU2413_OPLL_dump2patch

#ifdef __cplusplus
}
#endif

#endif

