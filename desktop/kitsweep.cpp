// Phase 14 gate: every kit, through the real solver, rendered in RGB565.
#include <cstdio>
#include <cmath>
#include <lvgl.h>
#include "/Users/francoraso/Documents/Development/Claude/ScoreDeck/firmware/ScoreDeck/src/ui/theme.h"
#include "/Users/francoraso/Documents/Development/Claude/ScoreDeck/firmware/ScoreDeck/src/core/state.h"
static const uint32_t KITS[] = {
 0xAF1E2D,0x00205B,0x00338D,0xE31837,0xFF4C00,0xC8102E,0xC6011F,0xFFB81C,0x0038A8,
 0x005A9C,0xFD5A1E,0x006778,0x5A1414,0x000E2F,0xFF8200,0x002967,0xA6192E,0x99D9D9,
 0x041E42,0x004C54,0x00843D,0xCE1126,0x6F263D,0xFCB514,0x154734,0x8C2633,0x111111,
 0x2F4F4F,0xB0B7BC,0x7C1415,0x0C2340,0xD50032,0x00539B,0x236192,0x1D428A };
static double lin(double c){c/=255.0;return c<=0.04045?c/12.92:pow((c+0.055)/1.055,2.4);} 
static double Y(uint32_t c){return 0.2126729*lin((c>>16)&255)+0.7151522*lin((c>>8)&255)+0.0721750*lin(c&255);} 
static double Ls(uint32_t c){double y=Y(c);return 116*(y>216.0/24389?cbrt(y):(841.0/108)*y+4.0/29)-16;}
static double cr(uint32_t a,uint32_t b){double x=Y(a),y=Y(b);if(x<y){double t=x;x=y;y=t;}return (x+0.05)/(y+0.05);} 
static uint32_t q565(uint32_t c){uint32_t r=(c>>16)&255,g=(c>>8)&255,b=c&255;
  r=(r>>3);g=(g>>2);b=(b>>3);r=(r<<3)|(r>>2);g=(g<<2)|(g>>4);b=(b<<3)|(b>>2);return (r<<16)|(g<<8)|b;}
static lv_color_t g_buf[800*10];
int main(){
  // themeInit() builds the linear LUT the solver needs, but also touches the
  // active screen — so LVGL needs a display registered first.
  lv_init();
  static lv_disp_draw_buf_t db; lv_disp_draw_buf_init(&db, g_buf, nullptr, 800*10);
  static lv_disp_drv_t dd; lv_disp_drv_init(&dd);
  dd.draw_buf=&db; dd.hor_res=800; dd.ver_res=480;
  dd.flush_cb=[](lv_disp_drv_t* d,const lv_area_t*,lv_color_t*){ lv_disp_flush_ready(d); };
  lv_disp_drv_register(&dd);
  themeInit();
  const uint32_t BAR = 0x1B2636;      // the lineup bar's fill
  int selLow=0,lblLow=0,darker=0,over=0,n=0; double worstSel=99,worstLbl=99,maxL=0;
  for(uint32_t k : KITS){
    n++;
    uint32_t fill=q565(teamInkFor(k,BAR));
    double c1=cr(fill,BAR);
    if(c1<3.0) selLow++;
    if(c1<worstSel) worstSel=c1;
    lv_color_t li=badgeInk(fill);
    uint32_t ink=(uint32_t)((li.ch.red<<3|li.ch.red>>2)<<16 | (li.ch.green<<2|li.ch.green>>4)<<8 | (li.ch.blue<<3|li.ch.blue>>2));
    double c2=cr(ink,fill);
    if(c2<4.5) lblLow++;
    if(c2<worstLbl) worstLbl=c2;
    if(Ls(fill)<Ls(0x2A3646)) darker++;
    uint32_t bf=q565(teamFill(k));
    if(Ls(bf)>maxL) maxL=Ls(bf);
    if(Ls(bf)>74.3) over++;
  }
  printf("kits %d\n", n);
  printf("  selected tab  <3.0:1 : %2d   (was 25)   worst %.2f:1\n", selLow, worstSel);
  printf("  label        <4.5:1 : %2d   (was  8)   worst %.2f:1\n", lblLow, worstLbl);
  printf("  selected darker than C_EDGE: %d   (was TOR at -7.4 L*)\n", darker);
  printf("  badge fill over L* 74.3    : %d   max rendered L* %.2f  (SEA was 83.44)\n", over, maxL);
  {
    uint32_t bosFill = q565(teamFill(0xFFB81C));
    lv_color_t bi = badgeInk(bosFill);
    uint32_t bink = (uint32_t)(((bi.ch.red<<3|bi.ch.red>>2)<<16) |
                               ((bi.ch.green<<2|bi.ch.green>>4)<<8) |
                                (bi.ch.blue<<3|bi.ch.blue>>2));
    printf("  badgeInk on clamped BOS    : %.2f:1  (fill L* %.2f)\n",
           cr(bink, bosFill), Ls(bosFill));
  }
  return (selLow||lblLow||darker||over)?1:0;
}
