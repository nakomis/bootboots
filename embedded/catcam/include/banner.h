#ifndef CATCAM_BANNER_H
#define CATCAM_BANNER_H

// Logged at start-up. It used to live in secrets.h, but was never secret (BOOT-10).
const char BANNER[] = R"WELCOME(
  
o.oOOOo.                                 o                o.oOOOo.                          oO
o     o                                O                  o     o                          OO
O     O                O           O   o                  O     O                O         oO
oOooOO.               oOo         oOo  O                  oOooOO.               oOo        Oo
o     `O .oOo. .oOo.   o           o   OoOo. .oOo.        o     `O .oOo. .oOo.   o   .oOo  oO
O      o O   o O   o   O           O   o   o OooO'        O      o O   o O   o   O   `Ooo.
o     .O o   O o   O   o           o   o   O O            o     .O o   O o   O   o       O Oo
`OooOO'  `OoO' `OoO'   `oO         `oO O   o `OoO'        `OooOO'  `OoO' `OoO'   `oO `OoO' oO
  
)WELCOME";

#endif // CATCAM_BANNER_H
