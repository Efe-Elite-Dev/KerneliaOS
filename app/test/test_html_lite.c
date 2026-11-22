/* html_lite icin host testleri. Derleme ve calistirma: app/README.md */
#include <stdio.h>
#include <string.h>
#include "../html_lite.h"

#define MAX_RUNS 128
static char text[4096], href[1024], dump[4096];
static hl_run runs[MAX_RUNS];
static int fails = 0;

/* Parcalari "{stil,kesme}metin[@adres]" bicimine cevirir, parcalar '|' ile ayrilir */
static void make_dump(const hl_doc *d){
    int k = 0;
    dump[0] = 0;
    for(int r = 0; r < d->run_n; r++){
        const hl_run *x = &d->runs[r];
        if(r) k += snprintf(dump + k, sizeof dump - k, "|");
        k += snprintf(dump + k, sizeof dump - k, "{%d,%d}", x->style, x->brk);
        k += snprintf(dump + k, sizeof dump - k, "%.*s", x->len, d->text + x->off);
        if(x->href_len)
            k += snprintf(dump + k, sizeof dump - k, "@%.*s", x->href_len, d->href + x->href_off);
    }
}

static void expect(const char *name, const char *html, const char *want){
    hl_doc d;
    d.text = text; d.text_cap = sizeof text;
    d.href = href; d.href_cap = sizeof href;
    d.runs = runs; d.run_cap = MAX_RUNS;
    int tr = hl_parse(&d, html, (int)strlen(html));
    make_dump(&d);
    if(tr || strcmp(dump, want) != 0){
        printf("FAIL %s\n     got:  %s (kesildi=%d)\n     want: %s\n", name, dump, tr, want);
        fails++;
    } else {
        printf("ok   %s\n", name);
    }
}

int main(void){
    expect("baslik ve paragraf",
        "<h1>Merhaba</h1><p>Bu  bir\n test.</p>",
        "{1,2}Merhaba|{0,2}Bu bir test.");

    expect("baglanti ve adres (entity'siz)",
        "x <a href=\"http://a.b/c?d=1&amp;e\">tikla</a> sonra",
        "{0,0}x |{10,0}tikla@http://a.b/c?d=1&amp;e|{0,0} sonra");

    expect("script ve yorum atlanir",
        "a<script>var x='<b>';</script>b<!-- <i>c</i> -->d",
        "{0,0}abd");

    expect("entity: lt gt amp nbsp sayisal ve bilinmeyen",
        "&lt;tag&gt; &amp; &nbsp;x &foo; &#65;&#x41;",
        "{0,0}<tag> & x &foo; A&#x41;");

    expect("br satir kesmesi",
        "a<br>b",
        "{0,0}a|{0,1}b");

    expect("img yer tutucu",
        "x<img src=a.png alt=q>y",
        "{0,0}x [resim] y");

    expect("tirnakli href icinde >",
        "<a href='u>v'>k</a>",
        "{10,0}k@u>v");

    expect("'<' tek basina metin",
        "a < b",
        "{0,0}a < b");

    expect("liste ogeleri",
        "<ul><li>bir</li><li>iki</li></ul>",
        "{0,2}- bir|{0,2}- iki");

    expect("head atlanir, buyuk harf etiket",
        "<html><head><title>T</title></head><body><H1>Merhaba</H1></body></html>",
        "{1,2}Merhaba");

    expect("kapanmamis etiket sonda",
        "abc<p",
        "{0,0}abc");

    /* Tampon taşması: kesilmeli, cokmemeli */
    {
        hl_doc d;
        char small[4];
        d.text = small; d.text_cap = sizeof small;
        d.href = href; d.href_cap = sizeof href;
        d.runs = runs; d.run_cap = MAX_RUNS;
        int tr = hl_parse(&d, "hello world", 11);
        if(tr == 1 && d.text_len < d.text_cap){ printf("ok   tampon dolunca kesilir\n"); }
        else { printf("FAIL tampon dolunca kesilir (kesildi=%d)\n", tr); fails++; }
    }

    printf("\n%s (%d hata)\n", fails ? "BASARISIZ" : "TUMU GECTI", fails);
    return fails ? 1 : 0;
}
