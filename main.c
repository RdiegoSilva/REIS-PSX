/*
 * REIS PSX - launcher de jogos de PS1 para PS2 (via POPStarter)
 *
 * Etapa 1: scanner de .VCD, menu navegavel e lancamento.
 * Etapa 2 (a fazer): capas (ART/<ID>_COV.png), VMC, favoritos, temas.
 *
 * Estrutura esperada no dispositivo USB (mass:):
 *   mass:/POPS/POPSTARTER.ELF   (o usuario coloca)
 *   mass:/POPS/POPS.ELF         (o usuario coloca)
 *   mass:/POPS/IOPRP252.IMG     (o usuario coloca)
 *   mass:/POPS/SLUS_007.94.Nome do Jogo.VCD
 *
 * IMPORTANTE: rode este ELF a partir de um launcher que ja tenha carregado
 * os modulos USB (uLaunchELF, FreeMcBoot). O REIS PSX NAO reseta o IOP.
 */

#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <libpad.h>
#include <gsKit.h>
#include <dmaKit.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>

#define POPS_DIR        "mass:/POPS/"
#define POPSTARTER_ELF  "mass:/POPS/POPSTARTER.ELF"
#define MAX_GAMES       512
#define VISIBLE_ROWS    12

typedef struct {
    char file[256];     /* nome do arquivo .VCD, ex: SLUS_007.94.Crash.VCD */
    char id[16];        /* ex: SLUS_007.94 (pode ficar vazio) */
    char name[200];     /* nome de exibicao */
} Game;

static Game games[MAX_GAMES];
static int game_count = 0;

static char pad_buf[256] __attribute__((aligned(64)));

/* ---------------------------------------------------------------- */
/* Utilidades                                                       */
/* ---------------------------------------------------------------- */

static int ends_with_vcd(const char *s)
{
    size_t n = strlen(s);
    if (n < 5) return 0;
    return toupper((unsigned char)s[n - 4]) == '.' &&
           toupper((unsigned char)s[n - 3]) == 'V' &&
           toupper((unsigned char)s[n - 2]) == 'C' &&
           toupper((unsigned char)s[n - 1]) == 'D';
}

/* Verifica padrao "XXXX_NNN.NN" nos primeiros 11 caracteres */
static int has_game_id(const char *s)
{
    if (strlen(s) < 12) return 0;
    for (int i = 0; i < 4; i++) if (!isupper((unsigned char)s[i])) return 0;
    if (s[4] != '_') return 0;
    for (int i = 5; i < 8; i++) if (!isdigit((unsigned char)s[i])) return 0;
    if (s[8] != '.') return 0;
    for (int i = 9; i < 11; i++) if (!isdigit((unsigned char)s[i])) return 0;
    return 1;
}

static int cmp_games(const void *a, const void *b)
{
    return strcasecmp(((const Game *)a)->name, ((const Game *)b)->name);
}

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    if (!in) return 0;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return 0; }

    static char buf[64 * 1024];
    size_t n;
    int ok = 1;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = 0; break; }
    }
    fclose(in);
    fclose(out);
    return ok;
}

/* ---------------------------------------------------------------- */
/* Scanner                                                          */
/* ---------------------------------------------------------------- */

static void scan_games(void)
{
    game_count = 0;

    DIR *d = opendir(POPS_DIR);
    if (!d) return;

    struct dirent *e;
    while ((e = readdir(d)) != NULL && game_count < MAX_GAMES) {
        if (!ends_with_vcd(e->d_name)) continue;

        Game *g = &games[game_count];
        memset(g, 0, sizeof(*g));
        strncpy(g->file, e->d_name, sizeof(g->file) - 1);

        /* nome sem extensao */
        char base[256];
        strncpy(base, e->d_name, sizeof(base) - 1);
        base[sizeof(base) - 1] = '\0';
        base[strlen(base) - 4] = '\0';

        if (has_game_id(base)) {
            memcpy(g->id, base, 11);
            g->id[11] = '\0';
            const char *nm = base + 11;
            if (*nm == '.' || *nm == ' ') nm++;
            strncpy(g->name, *nm ? nm : base, sizeof(g->name) - 1);
        } else {
            strncpy(g->name, base, sizeof(g->name) - 1);
        }
        game_count++;
    }
    closedir(d);

    qsort(games, game_count, sizeof(Game), cmp_games);
}

/* ---------------------------------------------------------------- */
/* Lancamento via POPStarter                                        */
/* O POPStarter descobre qual VCD abrir pelo nome do proprio ELF:   */
/*   XX.<nome do vcd sem extensao>.ELF                              */
/* Por isso criamos uma copia com esse nome e executamos ela.       */
/* ---------------------------------------------------------------- */

static int launch_game(const Game *g)
{
    char base[256];
    strncpy(base, g->file, sizeof(base) - 1);
    base[sizeof(base) - 1] = '\0';
    base[strlen(base) - 4] = '\0';

    char elf_path[400];
    snprintf(elf_path, sizeof(elf_path), POPS_DIR "XX.%s.ELF", base);

    if (!file_exists(elf_path)) {
        if (!file_exists(POPSTARTER_ELF)) return -1;     /* falta o POPSTARTER.ELF */
        if (!copy_file(POPSTARTER_ELF, elf_path)) return -2; /* falha ao copiar */
    }

    padPortClose(0, 0);
    padEnd();

    char *argv[1] = { elf_path };
    LoadExecPS2(elf_path, 1, argv);
    return -3; /* so chega aqui se falhar */
}

/* ---------------------------------------------------------------- */
/* Video e texto                                                    */
/* ---------------------------------------------------------------- */

static GSGLOBAL *gs;
static GSFONTM *font;

static void init_video(void)
{
    gs = gsKit_init_global();
    gs->PSM = GS_PSM_CT24;
    gs->PSMZ = GS_PSMZ_16S;
    gs->ZBuffering = GS_SETTING_OFF;
    gs->DoubleBuffering = GS_SETTING_ON;

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);

    gsKit_init_screen(gs);
    gsKit_mode_switch(gs, GS_ONESHOT);

    font = gsKit_init_fontm();
    gsKit_fontm_upload(gs, font);
    font->Spacing = 0.9f;
}

static void text(float x, float y, float scale, u64 color, const char *s)
{
    gsKit_fontm_print_scaled(gs, font, x, y, 3, scale, color, s);
}

/* ---------------------------------------------------------------- */
/* Controle                                                         */
/* ---------------------------------------------------------------- */

static void init_pad(void)
{
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);
    padInit(0);
    padPortOpen(0, 0, pad_buf);

    int state;
    do {
        state = padGetState(0, 0);
    } while (state != PAD_STATE_STABLE && state != PAD_STATE_FINDCTP1);
}

static u32 read_pad(void)
{
    struct padButtonStatus b;
    int state = padGetState(0, 0);
    if (state != PAD_STATE_STABLE && state != PAD_STATE_FINDCTP1) return 0;
    if (padRead(0, 0, &b) == 0) return 0;
    return 0xFFFF ^ b.btns;
}

/* ---------------------------------------------------------------- */
/* Main                                                             */
/* ---------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    SifInitRpc(0);

    init_video();
    init_pad();
    scan_games();

    int sel = 0, top = 0;
    u32 old = 0;
    char status[128] = "";

    const u64 WHITE  = GS_SETREG_RGBAQ(0xFF, 0xFF, 0xFF, 0x80, 0x00);
    const u64 GOLD   = GS_SETREG_RGBAQ(0xFF, 0xC8, 0x32, 0x80, 0x00);
    const u64 GRAY   = GS_SETREG_RGBAQ(0xA0, 0xA0, 0xA0, 0x80, 0x00);
    const u64 RED    = GS_SETREG_RGBAQ(0xFF, 0x50, 0x50, 0x80, 0x00);
    const u64 BG     = GS_SETREG_RGBAQ(0x10, 0x10, 0x30, 0x00, 0x00);
    const u64 PANEL  = GS_SETREG_RGBAQ(0x20, 0x20, 0x50, 0x40, 0x00);

    while (1) {
        u32 pad = read_pad();
        u32 pressed = pad & ~old;
        old = pad;

        if (game_count > 0) {
            if (pressed & PAD_DOWN) {
                sel = (sel + 1) % game_count;
            }
            if (pressed & PAD_UP) {
                sel = (sel - 1 + game_count) % game_count;
            }
            if (pressed & PAD_R1) {
                sel += VISIBLE_ROWS;
                if (sel >= game_count) sel = game_count - 1;
            }
            if (pressed & PAD_L1) {
                sel -= VISIBLE_ROWS;
                if (sel < 0) sel = 0;
            }
            if (sel < top) top = sel;
            if (sel >= top + VISIBLE_ROWS) top = sel - VISIBLE_ROWS + 1;

            if (pressed & PAD_CROSS) {
                int r = launch_game(&games[sel]);
                if (r == -1) snprintf(status, sizeof(status), "Falta mass:/POPS/POPSTARTER.ELF");
                else if (r == -2) snprintf(status, sizeof(status), "Falha ao preparar o ELF (pasta POPS gravavel?)");
                else snprintf(status, sizeof(status), "Falha ao iniciar o POPStarter");
                init_pad(); /* launch_game fechou o pad */
            }
        }

        if (pressed & PAD_SELECT) {
            scan_games();
            sel = top = 0;
            snprintf(status, sizeof(status), "Lista atualizada");
        }
        if (pressed & PAD_START) {
            Exit(0);
        }

        /* ---- desenho ---- */
        gsKit_clear(gs, BG);

        gsKit_prim_sprite(gs, 20, 20, gs->Width - 20, 60, 1, PANEL);
        text(36, 28, 1.2f, GOLD, "REIS PSX");
        text(250, 34, 0.7f, GRAY, "launcher de PS1 para PS2");

        gsKit_prim_sprite(gs, 20, 75, gs->Width - 20, gs->Height - 55, 1, PANEL);

        if (game_count == 0) {
            text(40, 110, 0.8f, RED, "Nenhum .VCD encontrado em mass:/POPS/");
            text(40, 140, 0.7f, GRAY, "Conecte o USB e aperte SELECT para atualizar.");
        } else {
            for (int i = 0; i < VISIBLE_ROWS; i++) {
                int idx = top + i;
                if (idx >= game_count) break;
                float y = 90 + i * 28;
                if (idx == sel) {
                    gsKit_prim_sprite(gs, 24, y - 4, gs->Width - 24, y + 22, 2,
                                      GS_SETREG_RGBAQ(0x40, 0x40, 0x90, 0x60, 0x00));
                }
                text(40, y, 0.8f, idx == sel ? GOLD : WHITE, games[idx].name);
            }

            char info[64];
            snprintf(info, sizeof(info), "%d/%d  %s", sel + 1, game_count,
                     games[sel].id[0] ? games[sel].id : "(sem ID)");
            text(36, gs->Height - 48, 0.6f, GRAY, info);
        }

        text(36, gs->Height - 30, 0.55f, GRAY,
             "X: jogar   SELECT: atualizar   L1/R1: pagina   START: sair");
        if (status[0]) text(36, gs->Height - 72, 0.6f, RED, status);

        gsKit_queue_exec(gs);
        gsKit_sync_flip(gs);
    }

    return 0;
}
