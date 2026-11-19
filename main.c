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
 *
 * Compile com "make DEMO=1" para gerar uma versao com jogos de mentira,
 * util para ver o layout no PCSX2 (que nao tem mass:).
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

/* ---- layout ---- */
#define MARGIN    24
#define ROW_H     28
#define LIST_W    392

/* Largura aproximada de um caractere da fonte com scale 1.0 (ja com o
 * Spacing aplicado). Medida a partir de um print; ajuste se o texto
 * ficar cortado ou com sobra. */
#define FONT_ADV  24.0f

#define COL(r, g, b, a) GS_SETREG_RGBAQ(r, g, b, a, 0x00)

#define C_TEXT     COL(0xF2, 0xF4, 0xFF, 0x80)
#define C_DIM      COL(0x8E, 0x96, 0xC4, 0x80)
#define C_GOLD     COL(0xFF, 0xC8, 0x32, 0x80)
#define C_GOLD_DIM COL(0xFF, 0xC8, 0x32, 0x40)
#define C_ERR      COL(0xFF, 0x7B, 0x7B, 0x80)
#define C_PANEL    COL(0x12, 0x18, 0x3C, 0x70)
#define C_SEL      COL(0x2E, 0x3F, 0xA8, 0x78)
#define C_TRACK    COL(0x25, 0x2C, 0x5E, 0x70)
#define C_KEY      COL(0xD8, 0xDC, 0xF5, 0x80)
#define C_DARK     COL(0x0A, 0x0D, 0x22, 0x80)
#define C_TOAST    COL(0x5A, 0x1C, 0x2C, 0x78)

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

#ifdef DEMO
static void fill_demo(void)
{
    static const char *ids[] = {
        "SCUS_944.00", "SCUS_944.26", "SLUS_007.94", "SLUS_012.51",
        "SCUS_944.88", "SLUS_005.94", "SLUS_009.58", "SCUS_942.28",
        "SLUS_004.02", "SLUS_005.52", "SLUS_006.62", "SLUS_013.14",
        "SLUS_007.76", "SLUS_010.30"
    };
    static const char *names[] = {
        "Crash Bandicoot", "Crash Team Racing", "Final Fantasy VII",
        "Final Fantasy IX", "Gran Turismo 2", "Metal Gear Solid",
        "Resident Evil 2", "Spyro the Dragon", "Tekken 3", "Tomb Raider",
        "Castlevania Symphony of the Night",
        "Tony Hawk's Pro Skater 2 Edicao Especial", "Silent Hill",
        "Vagrant Story"
    };
    for (int i = 0; i < 14 && game_count < MAX_GAMES; i++) {
        Game *g = &games[game_count++];
        memset(g, 0, sizeof(*g));
        snprintf(g->id, sizeof(g->id), "%s", ids[i]);
        snprintf(g->name, sizeof(g->name), "%s", names[i]);
        snprintf(g->file, sizeof(g->file), "%s.%s.VCD", ids[i], names[i]);
    }
}
#endif

static void scan_games(void)
{
    game_count = 0;

    DIR *d = opendir(POPS_DIR);
    if (d) {
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
    }

#ifdef DEMO
    if (game_count == 0) fill_demo();
#endif

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
/* Video e desenho                                                  */
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
    gs->PrimAlphaEnable = GS_SETTING_ON;   /* necessario para o texto ter fundo transparente */

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);

    gsKit_init_screen(gs);
    gsKit_set_primalpha(gs, GS_BLEND_BACK2FRONT, 0);
    gsKit_mode_switch(gs, GS_ONESHOT);

    font = gsKit_init_fontm();
    gsKit_fontm_upload(gs, font);
    font->Spacing = 0.9f;
}

static void rect(float x1, float y1, float x2, float y2, u64 c)
{
    gsKit_prim_sprite(gs, x1, y1, x2, y2, 1, c);
}

static void text(float x, float y, float scale, u64 color, const char *s)
{
    gsKit_fontm_print_scaled(gs, font, x, y, 3, scale, color, s);
}

static float text_w(const char *s, float scale)
{
    return (float)strlen(s) * FONT_ADV * scale;
}

static void text_c(float cx, float y, float scale, u64 color, const char *s)
{
    text(cx - text_w(s, scale) * 0.5f, y, scale, color, s);
}

static void text_r(float rx, float y, float scale, u64 color, const char *s)
{
    text(rx - text_w(s, scale), y, scale, color, s);
}

/* Copia s para out, cortando com "..." se passar de max caracteres.
 * out precisa ter pelo menos max+1 bytes. */
static void fit(char *out, size_t n, const char *s, int max)
{
    size_t len = strlen(s);
    if (max < 4) max = 4;
    if ((int)len <= max) {
        strncpy(out, s, n - 1);
        out[n - 1] = '\0';
        return;
    }
    size_t k = (size_t)(max - 3);
    if (k > n - 4) k = n - 4;
    memcpy(out, s, k);
    strcpy(out + k, "...");
}

/* Escreve o texto quebrando em ate max_lines linhas (quebra em espacos). */
static void draw_wrapped(float x, float y, float scale, u64 col, const char *s,
                         int max_chars, int max_lines, float line_h)
{
    char line[128];
    if (max_chars > 120) max_chars = 120;

    for (int l = 0; l < max_lines && *s; l++) {
        while (*s == ' ') s++;
        int len = (int)strlen(s);

        if (l == max_lines - 1 || len <= max_chars) {
            fit(line, sizeof(line), s, max_chars);
            text(x, y + l * line_h, scale, col, line);
            return;
        }

        int cut = max_chars;
        for (int i = max_chars; i > 0; i--) {
            if (s[i] == ' ') { cut = i; break; }
        }
        memcpy(line, s, (size_t)cut);
        line[cut] = '\0';
        text(x, y + l * line_h, scale, col, line);
        s += cut;
    }
}

/* Botao do rodape: "tecla" em destaque + legenda. Devolve o proximo x. */
static float hint(float x, float y, const char *key, const char *label)
{
    const float sc = 0.42f;
    float kw = text_w(key, sc) + 12.0f;
    rect(x, y, x + kw, y + 20, C_KEY);
    text(x + 6, y + 3, sc, C_DARK, key);
    text(x + kw + 6, y + 3, sc, C_DIM, label);
    return x + kw + 6 + text_w(label, sc) + 16.0f;
}

static void draw_background(void)
{
    float w = (float)gs->Width, h = (float)gs->Height;
    u64 top = COL(0x0B, 0x10, 0x2A, 0x80);
    u64 bot = COL(0x2A, 0x14, 0x55, 0x80);
    gsKit_prim_quad_gouraud(gs, 0, 0, w, 0, 0, h, w, h, 1, top, top, bot, bot);
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
    char status[64] = "";
    int status_t = 0;
    float hl_y = -1.0f;     /* posicao animada do destaque da selecao */

    while (1) {
        const float W = (float)gs->Width;
        const float H = (float)gs->Height;

        const float body_top = 78.0f;
        const float body_bot = H - 56.0f;
        const float list_x1  = MARGIN;
        const float list_x2  = MARGIN + LIST_W;
        const float side_x1  = list_x2 + 12.0f;
        const float side_x2  = W - MARGIN;

        int visible = (int)((body_bot - body_top - 16.0f) / ROW_H);
        if (visible < 1) visible = 1;

        /* ---- entrada ---- */
        u32 pad = read_pad();
        u32 pressed = pad & ~old;
        old = pad;

        if (game_count > 0) {
            if (pressed & PAD_DOWN) sel = (sel + 1) % game_count;
            if (pressed & PAD_UP)   sel = (sel - 1 + game_count) % game_count;
            if (pressed & PAD_R1) {
                sel += visible;
                if (sel >= game_count) sel = game_count - 1;
            }
            if (pressed & PAD_L1) {
                sel -= visible;
                if (sel < 0) sel = 0;
            }
            if (sel < top) top = sel;
            if (sel >= top + visible) top = sel - visible + 1;

            if (pressed & PAD_CROSS) {
                int r = launch_game(&games[sel]);
                if (r == -1)      snprintf(status, sizeof(status), "Falta POPSTARTER.ELF em POPS/");
                else if (r == -2) snprintf(status, sizeof(status), "Erro ao copiar POPSTARTER.ELF");
                else              snprintf(status, sizeof(status), "Falha ao iniciar POPStarter");
                status_t = 240;
                init_pad(); /* launch_game fechou o pad */
            }
        }

        if (pressed & PAD_SELECT) {
            scan_games();
            sel = top = 0;
            hl_y = -1.0f;
            snprintf(status, sizeof(status), "Lista atualizada");
            status_t = 120;
        }
        if (pressed & PAD_START) {
            Exit(0);
        }

        if (status_t > 0 && --status_t == 0) status[0] = '\0';

        /* ---- desenho ---- */
        draw_background();

        /* cabecalho */
        rect(MARGIN, 16, W - MARGIN, 60, C_PANEL);
        rect(MARGIN, 16, MARGIN + 4, 60, C_GOLD);
        rect(MARGIN, 60, W - MARGIN, 62, C_GOLD_DIM);
        text(MARGIN + 18, 24, 0.80f, C_GOLD, "REIS PSX");
        text(MARGIN + 18 + text_w("REIS PSX", 0.80f) + 14, 33, 0.42f, C_DIM, "launcher PS1");
        {
            char cnt[32];
            snprintf(cnt, sizeof(cnt), "%d %s", game_count, game_count == 1 ? "jogo" : "jogos");
            text_r(W - MARGIN - 14, 33, 0.45f, C_TEXT, cnt);
        }

        /* painel da lista */
        rect(list_x1, body_top, list_x2, body_bot, C_PANEL);

        if (game_count == 0) {
            float cx = (list_x1 + list_x2) * 0.5f;
            float cy = body_top + 70.0f;
            text_c(cx, cy,       0.60f, C_ERR,  "Nenhum jogo encontrado");
            text_c(cx, cy + 50,  0.45f, C_DIM,  "Coloque arquivos .VCD em");
            text_c(cx, cy + 74,  0.50f, C_GOLD, "mass:/POPS/");
            text_c(cx, cy + 104, 0.45f, C_DIM,  "e aperte SELECT para atualizar");
        } else {
            /* destaque animado da selecao */
            float target = body_top + 8.0f + (float)(sel - top) * ROW_H;
            if (hl_y < 0.0f) hl_y = target;
            hl_y += (target - hl_y) * 0.35f;
            rect(list_x1 + 6, hl_y, list_x2 - 16, hl_y + ROW_H - 2, C_SEL);
            rect(list_x1 + 6, hl_y, list_x1 + 9,  hl_y + ROW_H - 2, C_GOLD);

            const float name_sc = 0.55f;
            int max_chars = (int)((LIST_W - 20 - 28) / (FONT_ADV * name_sc));

            for (int i = 0; i < visible; i++) {
                int idx = top + i;
                if (idx >= game_count) break;
                float y = body_top + 8.0f + i * ROW_H;

                char shown[256];
                fit(shown, sizeof(shown), games[idx].name, max_chars);
                text(list_x1 + 20, y + 5, name_sc,
                     idx == sel ? C_GOLD : C_TEXT, shown);
            }

            /* barra de rolagem */
            if (game_count > visible) {
                float ty1 = body_top + 8.0f;
                float ty2 = body_bot - 8.0f;
                float th  = ty2 - ty1;
                float thumb_h = th * (float)visible / (float)game_count;
                if (thumb_h < 16.0f) thumb_h = 16.0f;
                float thumb_y = ty1 + (th - thumb_h) * (float)top /
                                (float)(game_count - visible);
                rect(list_x2 - 11, ty1, list_x2 - 7, ty2, C_TRACK);
                rect(list_x2 - 11, thumb_y, list_x2 - 7, thumb_y + thumb_h, C_GOLD);
            }
        }

        /* painel lateral: capa + informacoes */
        rect(side_x1, body_top, side_x2, body_bot, C_PANEL);

        {
            float s  = (side_x2 - side_x1) - 16.0f;
            float cx = side_x1 + 8.0f;
            float cy = body_top + 8.0f;
            float mx = cx + s * 0.5f;

            /* area da capa (placeholder ate implementarmos as capas) */
            gsKit_prim_quad_gouraud(gs, cx, cy, cx + s, cy, cx, cy + s, cx + s, cy + s, 1,
                                    COL(0x2A, 0x33, 0x7A, 0x80), COL(0x1B, 0x22, 0x55, 0x80),
                                    COL(0x1B, 0x22, 0x55, 0x80), COL(0x10, 0x14, 0x38, 0x80));
            rect(cx, cy, cx + s, cy + 2, C_GOLD_DIM);
            text_c(mx, cy + s * 0.5f - 18, 0.60f, C_DIM, "CAPA");
            text_c(mx, cy + s * 0.5f + 10, 0.40f, C_DIM, "em breve");

            if (game_count > 0) {
                const Game *g = &games[sel];
                int wrap = (int)(s / (FONT_ADV * 0.50f));

                draw_wrapped(cx, cy + s + 12, 0.50f, C_TEXT, g->name, wrap, 3, 20.0f);

                char info[48];
                snprintf(info, sizeof(info), "%s", g->id[0] ? g->id : "sem ID");
                text(cx, body_bot - 46, 0.42f, C_GOLD, info);
                snprintf(info, sizeof(info), "%d / %d", sel + 1, game_count);
                text(cx, body_bot - 26, 0.42f, C_DIM, info);
            }
        }

        /* aviso temporario */
        if (status[0]) {
            rect(list_x1 + 8, body_bot - 34, list_x2 - 8, body_bot - 8, C_TOAST);
            text(list_x1 + 18, body_bot - 28, 0.42f, C_TEXT, status);
        }

        /* rodape com os botoes */
        rect(MARGIN, H - 46, W - MARGIN, H - 18, C_PANEL);
        {
            float x = MARGIN + 12;
            float y = H - 42;
            x = hint(x, y, "X", "Jogar");
            x = hint(x, y, "SELECT", "Atualizar");
            x = hint(x, y, "L1/R1", "Pagina");
            hint(x, y, "START", "Sair");
        }

        gsKit_queue_exec(gs);
        gsKit_sync_flip(gs);
    }

    return 0;
}
