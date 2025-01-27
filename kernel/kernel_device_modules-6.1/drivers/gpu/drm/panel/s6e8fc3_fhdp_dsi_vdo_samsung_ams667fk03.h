#ifndef __S6E8FC3_FHDP_DSI_VDO_SAMSUNG_AMS667FK03__
#define __S6E8FC3_FHDP_DSI_VDO_SAMSUNG_AMS667FK03__

#define REGFLAG_CMD				0xFFFA
#define REGFLAG_DELAY			0xFFFC
#define REGFLAG_UDELAY			0xFFFB
#define REGFLAG_END_OF_TABLE	0xFFFD

#define BRIGHTNESS_HALF         2047
#define BRIGHTNESS_MAX          4095

enum MODE_ID {
	FHD_SDC60 = 0,
	FHD_SDC90 = 1,
	FHD_SDC120 = 2,
};

struct ba {
	u32 brightness;
	u32 alpha;
};

struct LCM_setting_table {
	unsigned int cmd;
	unsigned int count;
	unsigned char para_list[256];
};

static struct LCM_setting_table elvss_dim_dly_setting[] = {
	{REGFLAG_CMD,3, {0xF0, 0x5A, 0x5A}},
	{REGFLAG_CMD,4, {0xB0, 0x00, 0x0C, 0xB2}},
	{REGFLAG_CMD,2, {0xB2, 0x00}},
	{REGFLAG_CMD,3, {0xF0, 0xA5, 0xA5}},
};

/* -------------------------doze mode setting start------------------------- */
static struct LCM_setting_table AOD_off_setting[] = {
	{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_finger_HBM_on_setting[] = {
	{REGFLAG_CMD,3, {0xF0, 0x5A, 0x5A}},
	{REGFLAG_CMD,4, {0xB0, 0x00, 0x0C, 0xB2}},
	{REGFLAG_CMD,2, {0xB2, 0x30}},
	{REGFLAG_CMD,3, {0xF0, 0xA5, 0xA5}},
	/*HBM ON*/
	{REGFLAG_CMD,3, {0xF0, 0x5A, 0x5A}},
	{REGFLAG_CMD,2, {0x53, 0xE0}},
	{REGFLAG_CMD,3, {0x51, 0x00, 0x00}},
	{REGFLAG_CMD,3, {0xF0, 0xA5, 0xA5}},
	{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_setbrightness_normal[] = {
	{REGFLAG_CMD,3, {0xF0, 0x5A, 0x5A}},
	{REGFLAG_CMD,4, {0xB0, 0x00, 0x0C, 0xB2}},
	{REGFLAG_CMD,2, {0xB2, 0x30}},
	{REGFLAG_CMD,3, {0xF0, 0xA5, 0xA5}},

	{REGFLAG_CMD,3, {0xF0, 0x5A, 0x5A}},
	{REGFLAG_CMD,2, {0x53, 0x20}},
	{REGFLAG_CMD,3, {0x51, 0x00, 0x00}},
	{REGFLAG_CMD,3, {0xF0, 0xA5, 0xA5}},
	{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table AOD_on_setting[] = {
	{REGFLAG_CMD, 3, {0xF0, 0x5A, 0x5A}},
	{REGFLAG_CMD, 2, {0x53, 0x20}},
	{REGFLAG_CMD, 3, {0x51, 0x03, 0x00}},
	{REGFLAG_CMD, 3, {0xF0, 0xA5, 0xA5}},
};

static struct LCM_setting_table aod_high_bl_level[] = {
	{REGFLAG_CMD, 3, {0xF0, 0x5A, 0x5A}},
	{REGFLAG_CMD, 2, {0x53, 0x20}},
	{REGFLAG_CMD, 3, {0x51, 0x03, 0x00}},
	{REGFLAG_CMD, 3, {0xF0, 0xA5, 0xA5}},
};

static struct LCM_setting_table aod_low_bl_level[] = {
	{REGFLAG_CMD, 3, {0xF0, 0x5A, 0x5A}},
	{REGFLAG_CMD, 2, {0x53, 0x20}},
	{REGFLAG_CMD, 3, {0x51, 0x01, 0x80}},
	{REGFLAG_CMD, 3, {0xF0, 0xA5, 0xA5}},
};
/* -------------------------doze mode setting end------------------------- */

/* -------------------------frame mode switch start------------------------- */
static struct LCM_setting_table mode_switch_to_60[] = {
    {REGFLAG_CMD, 3, {0xF0,0x5A,0x5A}},
    {REGFLAG_CMD, 2, {0x60,0x21}},
    {REGFLAG_CMD, 2, {0xF7,0x0B}},
    {REGFLAG_CMD, 3, {0xF0,0xA5,0xA5}},
};

static struct LCM_setting_table mode_switch_to_120[] = {
    {REGFLAG_CMD, 3, {0xF0,0x5A,0x5A}},
    {REGFLAG_CMD, 2, {0x60,0x01}},
    {REGFLAG_CMD, 2, {0xF7,0x0B}},
    {REGFLAG_CMD, 3, {0xF0,0xA5,0xA5}},
};
/* -------------------------frame mode switch end------------------------- */

static struct LCM_setting_table lcm_seed_mode0[] = {
	{REGFLAG_CMD,3,{0xF0, 0x5A, 0x5A}},
	{REGFLAG_CMD,2,{0x80, 0x05}},
	{REGFLAG_CMD,2,{0xB1, 0x00}},
	{REGFLAG_CMD,4,{0xB0, 0x00, 0x01, 0xB1}},
	{REGFLAG_CMD,22,{0xB1, 0xBB,0x01,0x00,0x11,0xC9,0x03,0x0C,0x00,0xC7,0x1C,0xF0,0xDA,0xDF,0x02,0xD0,0xDB,0xE1,0x03,0xFF,0xFF,0xFF}},
	{REGFLAG_CMD,3,{0xF0, 0xA5, 0xA5}},
};

static struct LCM_setting_table lcm_seed_mode1[] = {
	{REGFLAG_CMD,3,{0xF0, 0x5A, 0x5A}},
	{REGFLAG_CMD,2,{0x80, 0x05}},
	{REGFLAG_CMD,2,{0xB1, 0x00}},
	{REGFLAG_CMD,4,{0xB0, 0x00, 0x01, 0xB1}},
	{REGFLAG_CMD,22,{0xB1, 0xBB,0x01,0x00,0x11,0xC9,0x03,0x0C,0x00,0xC7,0x1C,0xF0,0xDA,0xDF,0x02,0xD0,0xDB,0xE1,0x03,0xFF,0xFF,0xFF}},
	{REGFLAG_CMD,3,{0xF0, 0xA5, 0xA5}},
};

static struct LCM_setting_table lcm_seed_mode2[] = {
	{REGFLAG_CMD,3,{0xF0, 0x5A, 0x5A}},
	{REGFLAG_CMD,2,{0x80, 0x05}},
	{REGFLAG_CMD,2,{0xB1, 0x00}},
	{REGFLAG_CMD,4,{0xB0, 0x00, 0x01, 0xB1}},
	{REGFLAG_CMD,22,{0xB1, 0xBB,0x01,0x00,0x11,0xC9,0x03,0x0C,0x00,0xC7,0x1C,0xF0,0xDA,0xDF,0x02,0xD0,0xDB,0xE1,0x03,0xFF,0xFF,0xFF}},
	{REGFLAG_CMD,3,{0xF0, 0xA5, 0xA5}},
};

static struct LCM_setting_table lcm_seed_mode3[] = {
	{REGFLAG_CMD,3,{0xF0, 0x5A, 0x5A}},
	{REGFLAG_CMD,2,{0x80, 0x05}},
	{REGFLAG_CMD,2,{0xB1, 0x00}},
	{REGFLAG_CMD,4,{0xB0, 0x00, 0x01, 0xB1}},
	{REGFLAG_CMD,22,{0xB1, 0xBB,0x01,0x00,0x11,0xC9,0x03,0x0C,0x00,0xC7,0x1C,0xF0,0xDA,0xDF,0x02,0xD0,0xDB,0xE1,0x03,0xFF,0xFF,0xFF}},
	{REGFLAG_CMD,3,{0xF0, 0xA5, 0xA5}},
};

static struct ba brightness_seed_alpha_lut_dc[] = {
	{0, 2000},
	{12, 1959},
	{30, 1956},
	{60, 1953},
	{90, 1942},
	{100, 1938},
	{140, 1930},
	{200, 1920},
	{300, 1860},
	{400, 1790},
	{500, 1700},
	{600, 1560},
	{700, 1400},
	{800, 1210},
	{900, 980},
	{1000, 690},
	{1100, 380},
	{1200, 0},
};

#endif
