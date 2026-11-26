// Keystone's reactive layer (0.6.1), on top of whatever the projector plays.
//
// Keystone sets these about 30 times a second over mpv's IPC socket (glsl-shader-opts,
// gpu-next only), from what the bass is doing and which effects are on. Every effect stays
// inside the Max 115's grille and hypervent, and the badge box is always left exactly as the
// video has it, so the Peavey logo and its glint never change. Positions are fractions of
// the 1024 x 768 alignment card.

//!PARAM ks_on
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 1.0
0.0

//!PARAM ks_bright
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 2.0
0.0

//!PARAM ks_pulse
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 1.0
0.0

//!PARAM ks_tr
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 1.0
1.0

//!PARAM ks_tg
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 1.0
1.0

//!PARAM ks_tb
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 1.0
1.0

//!PARAM ks_tint
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 1.0
0.0

//!PARAM ks_glitch
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 1.0
0.0

//!PARAM ks_seed
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 1000.0
0.0

//!PARAM ks_smear
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 1.0
0.0

//!PARAM ks_drift
//!TYPE DYNAMIC float
//!MINIMUM -0.2
//!MAXIMUM 0.2
0.0

//!PARAM ks_bloom
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 1.0
0.0

//!PARAM ks_haze
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 1.0
0.0

//!HOOK MAIN
//!BIND HOOKED
//!DESC Keystone reactive layer

float ks_hash (vec2 p) { return fract (sin (dot (p, vec2 (12.9898, 78.233))) * 43758.5453); }

// the grille and the hypervent below it
bool ks_inGrille (vec2 p)
{
    if (p.x >= 0.0957 && p.x <= 0.9043 && p.y <= 0.7422) return true;
    if (p.y > 0.7422 && p.y <= 0.8464)
    {
        float k = (p.y - 0.7422) / 0.1042;
        float l = mix (0.2764, 0.3438, k), r = mix (0.7275, 0.6699, k);
        return p.x >= l && p.x <= r;
    }
    return p.y > 0.8464 && p.x >= 0.3438 && p.x <= 0.6699;
}

bool ks_inBadge (vec2 p) { return p.x >= 0.4102 && p.x <= 0.5977 && p.y >= 0.0339 && p.y <= 0.2148; }

vec4 hook()
{
    vec2 p = HOOKED_pos;
    vec4 orig = HOOKED_tex (p);
    if (ks_on < 0.5 || ! ks_inGrille (p) || ks_inBadge (p)) return orig;

    // a push in from the middle of the grille on each note
    vec2 c = vec2 (0.5, 0.37);
    vec2 q = c + (p - c) / (1.0 + 0.06 * ks_pulse);
    q.x += ks_drift;

    // Bitrot: rows torn sideways, colours split
    if (ks_glitch > 0.0)
    {
        float row = floor (q.y * 28.0);
        if (ks_hash (vec2 (row, ks_seed)) < ks_glitch * 0.55)
            q.x += (ks_hash (vec2 (row + 7.0, ks_seed)) - 0.5) * 0.22 * ks_glitch;
        float blockX = floor (q.x * 12.0), blockY = floor (q.y * 9.0);
        if (ks_hash (vec2 (blockX + ks_seed, blockY)) < ks_glitch * 0.12)
            q = vec2 (floor (q.x * 24.0) / 24.0, floor (q.y * 18.0) / 18.0);
    }
    q = clamp (q, vec2 (0.0), vec2 (1.0));
    vec4 col = HOOKED_tex (q);
    if (ks_glitch > 0.0)
    {
        float s = 0.012 * ks_glitch;
        col.r = HOOKED_tex (clamp (q + vec2 (s, 0.0), vec2 (0.0), vec2 (1.0))).r;
        col.b = HOOKED_tex (clamp (q - vec2 (s, 0.0), vec2 (0.0), vec2 (1.0))).b;
    }

    // Speckle Hold: the frozen frame smears sideways
    if (ks_smear > 0.0)
    {
        vec4 acc = col;
        for (int i = 1; i <= 6; ++i)
            acc += HOOKED_tex (clamp (q - vec2 (float (i) * 0.014 * ks_smear, 0.0), vec2 (0.0), vec2 (1.0)));
        col = mix (col, acc / 7.0, ks_smear);
    }

    // delay and reverb: a soft haze
    if (ks_haze > 0.0)
    {
        vec2 d = HOOKED_pt * 3.0;
        vec4 b = (HOOKED_tex (q + d) + HOOKED_tex (q - d) + HOOKED_tex (q + vec2 (d.x, -d.y)) + HOOKED_tex (q + vec2 (-d.x, d.y))) * 0.25;
        col = mix (col, b, ks_haze);
        col.rgb = mix (col.rgb, vec3 (0.82, 0.86, 1.0), 0.22 * ks_haze);
    }

    // how hard you play
    col.rgb *= 1.0 + ks_bright;

    // the note's colour: keep the picture's light, take the note's hue
    float l = dot (col.rgb, vec3 (0.299, 0.587, 0.114));
    col.rgb = mix (col.rgb, vec3 (ks_tr, ks_tg, ks_tb) * l * 1.7, ks_tint);

    // Afterglow: a bloom from the middle
    float r = distance (p, c);
    col.rgb += vec3 (1.0, 0.86, 0.66) * ks_bloom * exp (-r * r * 9.0) * 0.8;

    return vec4 (clamp (col.rgb, 0.0, 1.0), orig.a);
}
