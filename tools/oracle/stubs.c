/* Link stubs for the trace oracle: everything dm_game_main.c, joy.c, main1x.c, calc.c
 * and dm_virus_init.c call outside the game logic (graphics, audio, EEPROM, the OS,
 * the CPU player). None of them feed back into a 2P human VS round; the ones that do
 * something (input, time, sound log) live in trace_oracle.c. Signatures do not matter
 * to the linker, so the plain ones are declared as int f(void) and return 0. */
#include <stdint.h>

#define Z(name) int name(void) { return 0; }
Z(BgTasksManager_Destroy) Z(BgTasksManager_GetRemainingTasks) Z(BgTasksManager_Init)
Z(BgTasksManager_SendTask) Z(CopyTexBlock16) Z(CopyTexBlock8) Z(DebugMenu_CharacterEdit_Draw)
Z(DebugMenu_CharacterEdit_Update) Z(DebugMenu_Settings_Draw) Z(DebugMenu_Settings_Update)
Z(DecompressRomToRam) Z(EepRom_DumpErrMes) Z(EepRom_Init) Z(EepRom_InitFirst) Z(EepRom_WriteAll)
Z(EndingLastMessage) Z(F3ClearFZRtn) Z(F3RCPinitRtn) Z(FillRectRGBA) Z(RecWritingMsg_calc)
Z(RecWritingMsg_draw) Z(RecWritingMsg_init) Z(RecWritingMsg_setPos) Z(RecWritingMsg_setStr)
Z(RecWritingMsg_start) Z(RectAlphaTexTile) Z(StretchAlphaTexBlock) Z(StretchAlphaTexTile)
Z(StretchTexTile4) Z(StretchTexTile4i) Z(_dm_seq_play_in_game) Z(add_attack_effect)
Z(aiCOM_MissTake) Z(aifGameInit) Z(aifKeyOut) Z(aifMake) Z(aifMakeFlagSet) Z(alpha_texture_init_dl)
Z(animeSeq_isEnd) Z(animeSmog_draw) Z(animeSmog_init) Z(animeSmog_load) Z(animeSmog_start)
Z(animeSmog_stop) Z(animeSmog_update) Z(animeState_draw) Z(animeState_initDL) Z(animeState_initDL2)
Z(animeState_load) Z(animeState_set) Z(animeState_update) Z(curtain_proc) Z(disp_allclear_logo)
Z(disp_attack_effect) Z(disp_clear_logo) Z(disp_continue_logo) Z(disp_continue_logo_score)
Z(disp_count_logo) Z(disp_draw_logo) Z(disp_gameover_logo) Z(disp_logo_setup) Z(disp_lose_logo)
Z(disp_pause_logo) Z(disp_retire_logo) Z(disp_timeover_logo) Z(disp_timestop_logo) Z(disp_win_logo)
Z(dm_audio_is_stopped) Z(dm_audio_stop) Z(dm_audio_update) Z(dm_level_sort_set) Z(dm_seq_play_in_game)
Z(dm_seq_set_volume) Z(dm_story_sort_set) Z(dm_taiQ_sort_set) Z(dm_timeAt_sort_set) Z(dm_vc_fl_set)
Z(dm_vm_fl_set) Z(dm_vm_ta_set) Z(dm_vscom_set) Z(dm_vsman_set) Z(drawCursorPattern) Z(draw_Tex)
Z(draw_coffee_break) Z(draw_menu_bg) Z(func_8007E760) Z(get_angle) Z(gfxSetScissor) Z(gfxTaskStart)
Z(guMtxF2L) Z(guOrtho) Z(guRotateRPYF) Z(guTranslate) Z(initEtcWork) Z(init_coffee_break)
Z(init_coffee_break_cnt) Z(init_menu_bg) Z(init_pause_disp) Z(load_TexBlock_4b) Z(load_TexPal)
Z(load_TexTile_4b) Z(msgWnd_addStr) Z(msgWnd_clear) Z(msgWnd_draw) Z(msgWnd_getHeight)
Z(msgWnd_getWidth) Z(msgWnd_init2) Z(msgWnd_isEnd) Z(msgWnd_isScroll) Z(msgWnd_isSpeaking)
Z(msgWnd_skip) Z(msgWnd_update) Z(nnScAddClient) Z(nnScRemoveClient) Z(normal_texture_init_dl)
Z(osContGetQuery) Z(osContInit) Z(osContSetCh) Z(osContStartQuery) Z(osContStartReadData)
Z(osCreateMesgQueue) Z(osRecvMesg) Z(osSetEventMesg) Z(osSetThreadPri) Z(osSetTimer)
Z(osVirtualToPhysical) Z(osWritebackDCacheAll) Z(replay_play) Z(replay_play_init) Z(replay_record)
Z(replay_record_init) Z(replay_record_init_buffer) Z(setSleepTimer) Z(sgenrand) Z(sins) Z(sqrt_a2b2)
Z(story_bg_init) Z(story_bg_proc) Z(tiCopyTexBlock) Z(tiLoadTexData) Z(tiStretchAlphaTexItem)
Z(tiStretchTexBlock) Z(tiStretchTexItem) Z(tiStretchTexTile) Z(genrand)

/* Data the logic only touches in paths a 2P human VS round never takes, sized generously. */
_Alignas(16) uint8_t B_800FAE80[0x1000], evs_mem_data[0x4000], gFramebuffers[2 * 320 * 240 * 2];
_Alignas(16) uint8_t gGfxGlist[0x40000], _romDataTbl[0x1000], ai_char_data[0x1000];
_Alignas(16) uint8_t Heap_bufferp[1 << 20];
char st_staffroll_txt[1];
int8_t D_80088105;
int8_t aiDebugP1 = -1; /* THINKTYPE_INVALID: no debug CPU on player 1 */
int fool_mode, s_hard_mode, gCurrentFramebufferIndex, graphic_no, pendingGFX, story_proc_no;
uint32_t gfx_gtask_no;
void *gGfxHead;
int main_no = 5; /* MAIN_GAME: started_game_flg starts false, as in a real round */
/* Character voice SEs are _charSE_tbl[charNo] + k; 200 + k marks them in the log. */
const uint8_t _charSE_tbl[64] = { [0 ... 63] = 200 };
