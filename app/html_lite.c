/* WindOS tarayici - HTML ayristirici (1. asama). Bkz. html_lite.h */
#include "html_lite.h"

typedef struct {
    hl_doc *d;
    int heading;            /* 0 ya da 1..6 */
    int link;               /* <a> icinde mi */
    int href_off, href_len; /* etkin baglanti adresi */
    int pending;            /* siradaki yazilan parcadan once gelecek kesme */
    int line_start;         /* satir basi: bosluklari atla */
    int last_space;         /* son yazilan karakter bosluk */
    int cur_off, cur_len;   /* acik parca */
    int cur_style, cur_brk, cur_href_off, cur_href_len;
    int truncated;
} hl_st;

static int hl_ws(int c){ return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
static int hl_lower(int c){ return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int hl_alpha(int c){ c = hl_lower(c); return c >= 'a' && c <= 'z'; }
static int hl_alnum(int c){ return hl_alpha(c) || (c >= '0' && c <= '9'); }

/* s[a..b) adi (buyuk/kucuk harf farketmeden) name ile esit mi */
static int hl_name_is(const char *s, int a, int b, const char *name){
    int i = 0;
    for(; a + i < b && name[i]; i++)
        if(hl_lower((unsigned char)s[a + i]) != name[i]) return 0;
    return a + i == b && name[i] == 0;
}

/* h1..h6 ise seviyesi (1..6), degilse 0 */
static int hl_level(const char *s, int a, int b){
    if(b - a != 2 || hl_lower((unsigned char)s[a]) != 'h') return 0;
    int c = s[a + 1];
    return (c >= '1' && c <= '6') ? c - '0' : 0;
}

/* Paragraf / blok etiketi mi (li dahil; li'ye ek olarak "- " yazilir) */
static int hl_block(const char *s, int a, int b){
    static const char *const names[] = {
        "p", "div", "ul", "ol", "li", "table", "tr", "section", "article",
        "header", "footer", "blockquote", "hr"
    };
    for(unsigned k = 0; k < sizeof names / sizeof names[0]; k++)
        if(hl_name_is(s, a, b, names[k])) return 1;
    return 0;
}

static int hl_style(const hl_st *st){ return st->link ? HL_STYLE_LINK : st->heading; }

/* Acik parcayi kapat (stil/baglanti degisince ya da kesmede) ve yenisini ac */
static void hl_close(hl_st *st){
    hl_doc *d = st->d;
    if(st->cur_len > 0){
        if(d->run_n < d->run_cap){
            hl_run *r = &d->runs[d->run_n++];
            r->off = st->cur_off;
            r->len = st->cur_len;
            r->style = st->cur_style;
            r->brk = st->cur_brk;
            r->href_off = st->cur_href_off;
            r->href_len = st->cur_href_len;
        } else {
            st->truncated = 1;
        }
    }
    st->cur_off = d->text_len;
    st->cur_len = 0;
    st->cur_brk = HL_BRK_NONE;
    st->cur_style = hl_style(st);
    st->cur_href_off = st->link ? st->href_off : 0;
    st->cur_href_len = st->link ? st->href_len : 0;
}

/* Satir ya da paragraf kesmesi iste; bir sonraki yazilan parcaya uygulanir */
static void hl_break(hl_st *st, int kind){
    if(kind > st->pending) st->pending = kind;
    st->line_start = 1;
    st->last_space = 1;
    hl_close(st);
}

static void hl_putc(hl_st *st, char c){
    hl_doc *d = st->d;
    if(d->text_len + 1 >= d->text_cap){ st->truncated = 1; return; }
    if(st->cur_len == 0 && st->pending){
        st->cur_brk = st->pending;
        st->pending = HL_BRK_NONE;
    }
    d->text[d->text_len++] = c;
    st->cur_len++;
    st->line_start = 0;
    st->last_space = (c == ' ');
}

/* Bosluk yaz; satir basinda ya da arka arkaya bosluklar tek bosluga iner */
static void hl_space(hl_st *st){
    if(!st->line_start && !st->last_space) hl_putc(st, ' ');
}

/* Duz metin yaz (sabit metinler icin: "- ", "[resim] ") */
static void hl_lit(hl_st *st, const char *s){
    hl_space(st);
    for(int k = 0; s[k]; k++) hl_putc(st, s[k]);
}

/* s[i]=='&' ise karakteri cozer; *next sonraki indeks. 0: cozulemedi */
static int hl_entity(const char *s, int i, int b, char *out, int *next){
    static const struct { const char *name; char ch; } ents[] = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", ' '}
    };
    int j = i + 1;
    if(j < b && s[j] == '#'){
        int v = 0, digits = 0;
        j++;
        while(j < b && s[j] >= '0' && s[j] <= '9' && digits < 6){
            v = v * 10 + (s[j] - '0');
            j++;
            digits++;
        }
        if(digits == 0 || j >= b || s[j] != ';') return 0;
        *out = (v >= 32 && v < 127) ? (char)v : '?';
        *next = j + 1;
        return 1;
    }
    for(unsigned e = 0; e < sizeof ents / sizeof ents[0]; e++){
        int m = 0;
        while(ents[e].name[m] && j + m < b && s[j + m] == ents[e].name[m]) m++;
        if(ents[e].name[m] == 0 && j + m < b && s[j + m] == ';'){
            *out = ents[e].ch;
            *next = j + m + 1;
            return 1;
        }
    }
    return 0;
}

static void hl_text(hl_st *st, const char *s, int a, int b){
    int i = a;
    while(i < b){
        if(s[i] == '&'){
            char ch;
            int nx;
            if(hl_entity(s, i, b, &ch, &nx)){
                if(hl_ws((unsigned char)ch)) hl_space(st);
                else hl_putc(st, ch);
                i = nx;
                continue;
            }
        }
        if(hl_ws((unsigned char)s[i])) hl_space(st);
        else hl_putc(st, s[i]);
        i++;
    }
}

/* Baglanti adresini href tamponuna kopyala */
static void hl_href(hl_st *st, const char *s, int a, int b){
    hl_doc *d = st->d;
    st->href_off = d->href_len;
    st->href_len = 0;
    for(int i = a; i < b; i++){
        if(d->href_len + 1 >= d->href_cap){ st->truncated = 1; break; }
        d->href[d->href_len++] = s[i];
        st->href_len++;
    }
    if(d->href_len < d->href_cap) d->href[d->href_len] = 0;
}

/* name etiketinin kapanisini ara (buyuk/kucuk harf farketmez); yoksa n */
static int hl_find_close(const char *s, int n, int from, const char *name){
    int len = 0;
    while(name[len]) len++;
    for(int i = from; i + 1 < n; i++){
        if(s[i] != '<' || s[i + 1] != '/') continue;
        int m = 0;
        while(m < len && i + 2 + m < n && hl_lower((unsigned char)s[i + 2 + m]) == name[m]) m++;
        if(m == len) return i;
    }
    return n;
}

/* Etiketi isle; donus: etiketten sonraki indeks (her zaman i'den buyuk) */
static int hl_tag(hl_st *st, const char *s, int n, int i){
    int j = i + 1;
    if(j >= n){ hl_putc(st, '<'); return n; }

    /* yorum <!-- ... -->, doctype ve <? ?> : yok say */
    if(s[j] == '!' || s[j] == '?'){
        if(j + 2 < n && s[j + 1] == '-' && s[j + 2] == '-'){
            for(int k = j + 3; k + 2 < n; k++)
                if(s[k] == '-' && s[k + 1] == '-' && s[k + 2] == '>') return k + 3;
            return n;
        }
        for(int k = j; k < n; k++) if(s[k] == '>') return k + 1;
        return n;
    }

    int closing = 0;
    if(s[j] == '/'){ closing = 1; j++; }
    if(j >= n || !hl_alpha((unsigned char)s[j])){ hl_putc(st, '<'); return i + 1; }

    int ns = j;
    while(j < n && hl_alnum((unsigned char)s[j])) j++;
    int ne = j;

    /* oznitelikler: yalnizca href'i saklariz; tirnakli degerlerde '>' guvenli */
    int href_a = -1, href_b = -1, gt = -1;
    while(j < n){
        unsigned char c = (unsigned char)s[j];
        if(c == '>'){ gt = j; break; }
        if(hl_ws(c) || c == '/'){ j++; continue; }
        int as = j;
        while(j < n && !hl_ws((unsigned char)s[j]) && s[j] != '=' && s[j] != '>') j++;
        int ae = j;
        while(j < n && hl_ws((unsigned char)s[j])) j++;
        int vs = -1, ve = -1;
        if(j < n && s[j] == '='){
            j++;
            while(j < n && hl_ws((unsigned char)s[j])) j++;
            if(j < n && (s[j] == '"' || s[j] == '\'')){
                char q = s[j++];
                vs = j;
                while(j < n && s[j] != q) j++;
                ve = j;
                if(j < n) j++;
            } else {
                vs = j;
                while(j < n && !hl_ws((unsigned char)s[j]) && s[j] != '>') j++;
                ve = j;
            }
        }
        if(vs >= 0 && hl_name_is(s, as, ae, "href")){ href_a = vs; href_b = ve; }
    }
    int tend = (gt >= 0) ? gt + 1 : n;

    /* script, style ve head icerigi yazilmaz: kapanisina kadar atla */
    const char *skip = 0;
    if(hl_name_is(s, ns, ne, "script")) skip = "script";
    else if(hl_name_is(s, ns, ne, "style")) skip = "style";
    else if(hl_name_is(s, ns, ne, "head")) skip = "head";
    if(skip && !closing){
        int c = hl_find_close(s, n, tend, skip);
        if(c >= n){
            /* head kapanmamissa sayfa icerigini kaybetme; script/style'da sona kadar git */
            return (skip[0] == 'h') ? tend : n;
        }
        int g = c;
        while(g < n && s[g] != '>') g++;
        return (g < n) ? g + 1 : n;
    }

    int lv = hl_level(s, ns, ne);
    if(closing){
        if(hl_name_is(s, ns, ne, "a")){
            st->link = 0;
            hl_close(st);
        } else if(lv){
            st->heading = 0;
            hl_break(st, HL_BRK_PARA);
        } else if(hl_block(s, ns, ne)){
            hl_break(st, HL_BRK_PARA);
        }
        return tend;
    }

    if(hl_name_is(s, ns, ne, "br")){ hl_break(st, HL_BRK_LINE); return tend; }
    if(hl_name_is(s, ns, ne, "img")){ hl_lit(st, "[resim] "); return tend; }
    if(lv){
        st->heading = lv;
        hl_break(st, HL_BRK_PARA);
        return tend;
    }
    if(hl_name_is(s, ns, ne, "a")){
        st->link = 1;
        st->href_off = 0;
        st->href_len = 0;
        if(href_a >= 0) hl_href(st, s, href_a, href_b);
        hl_close(st);
        return tend;
    }
    if(hl_block(s, ns, ne)){
        hl_break(st, HL_BRK_PARA);
        if(hl_name_is(s, ns, ne, "li")) hl_lit(st, "- ");
        return tend;
    }
    return tend;   /* bilinmeyen etiket: yok say */
}

int hl_parse(hl_doc *d, const char *s, int n){
    hl_st st;
    st.d = d;
    st.heading = 0;
    st.link = 0;
    st.href_off = 0;
    st.href_len = 0;
    st.pending = HL_BRK_NONE;
    st.line_start = 1;
    st.last_space = 1;
    st.cur_off = 0;
    st.cur_len = 0;
    st.cur_style = HL_STYLE_NORMAL;
    st.cur_brk = HL_BRK_NONE;
    st.cur_href_off = 0;
    st.cur_href_len = 0;
    st.truncated = 0;

    d->text_len = 0;
    d->run_n = 0;
    d->href_len = 0;
    hl_close(&st);

    int i = 0;
    while(i < n){
        if(s[i] == '<'){
            i = hl_tag(&st, s, n, i);
            continue;
        }
        int j = i;
        while(j < n && s[j] != '<') j++;
        hl_text(&st, s, i, j);
        i = j;
    }
    hl_close(&st);

    if(d->text_len < d->text_cap) d->text[d->text_len] = 0;
    if(d->href_len < d->href_cap) d->href[d->href_len] = 0;
    return st.truncated;
}
