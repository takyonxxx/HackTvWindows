#include "hacktvlib.h"
#include <stdint.h>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <getopt.h>
#include <signal.h>
#include <cstdarg>
#include <cstdio>

#include "hacktv/av.h"

#define VERSION "1.0"

const char* getCurrentTimestamp() {
    static char buffer[26];
    time_t now = time(0);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return buffer;
}

static int _parse_ratio(rational_t *r, const char *s)
{
    int i;
    int64_t e;

    i = sscanf(s, "%d%*[:/]%d", &r->num, &r->den);
    if(i != 2 || r->den == 0)
    {
        return(HACKTV_ERROR);
    }

    e = gcd(r->num, r->den);
    r->num /= e;
    r->den /= e;

    return(HACKTV_OK);
}

static int _fputs_json(const char *str, FILE *stream)
{
    int c;

    for(c = 0; *str; str++)
    {
        const char *s = NULL;
        int r;

        switch(*str)
        {
        case '"': s = "\\\""; break;
        case '\\': s = "\\\\"; break;
        //case '/': s = "\\/"; break;
        case '\b': s = "\\b"; break;
        case '\f': s = "\\f"; break;
        case '\n': s = "\\n"; break;
        case '\r': s = "\\r"; break;
        case '\t': s = "\\t"; break;
        }

        if(s) r = fputs(s, stream);
        else r = fputc(*str, stream) == EOF ? EOF : 1;

        if(r == EOF)
        {
            return(c > 0 ? c : EOF);
        }

        c += r;
    }

    return(c);
}

/* List all avaliable modes, optionally formatted as a JSON array */
static void _list_modes(int json)
{
    const vid_configs_t *vc;

    if(json) printf("[\n");

    /* Load the mode configuration */
    for(vc = vid_configs; vc->id != NULL; vc++)
    {
        if(json)
        {
            printf("  {\n    \"id\": \"");
            _fputs_json(vc->id, stdout);
            printf("\",\n    \"description\": \"");
            _fputs_json(vc->desc ? vc->desc : "", stdout);
            printf("\"\n  }%s\n", vc[1].id != NULL ? "," : "");
        }
        else
        {
            printf("  %-14s = %s\n", vc->id, vc->desc ? vc->desc : "");
        }
    }

    if(json) printf("]\n");
}

// Custom strdup implementation (if needed)
char* strdup(const char* str) {
    if (str == nullptr) {
        return nullptr;
    }
    size_t len = strlen(str) + 1;  // strlen without the std:: prefix
    char* copy = static_cast<char*>(std::malloc(len));
    if (copy) {
        std::strcpy(copy, str);
    }
    return copy;
}

enum {
    _OPT_TELETEXT = 1000,
    _OPT_WSS,
    _OPT_VIDEOCRYPT,
    _OPT_VIDEOCRYPT2,
    _OPT_VIDEOCRYPTS,
    _OPT_SYSTER,
    _OPT_SYSTERAUDIO,
    _OPT_EUROCRYPT,
    _OPT_ACP,
    _OPT_VITS,
    _OPT_VITC,
    _OPT_FILTER,
    _OPT_NOCOLOUR,
    _OPT_NOAUDIO,
    _OPT_NONICAM,
    _OPT_A2STEREO,
    _OPT_SINGLE_CUT,
    _OPT_DOUBLE_CUT,
    _OPT_SCRAMBLE_AUDIO,
    _OPT_CHID,
    _OPT_MAC_AUDIO_STEREO,
    _OPT_MAC_AUDIO_MONO,
    _OPT_MAC_AUDIO_HIGH_QUALITY,
    _OPT_MAC_AUDIO_MEDIUM_QUALITY,
    _OPT_MAC_AUDIO_COMPANDED,
    _OPT_MAC_AUDIO_LINEAR,
    _OPT_MAC_AUDIO_L1_PROTECTION,
    _OPT_MAC_AUDIO_L2_PROTECTION,
    _OPT_SIS,
    _OPT_SWAP_IQ,
    _OPT_OFFSET,
    _OPT_PASSTHRU,
    _OPT_INVERT_VIDEO,
    _OPT_RAW_BB_FILE,
    _OPT_RAW_BB_BLANKING,
    _OPT_RAW_BB_WHITE,
    _OPT_SECAM_FIELD_ID,
    _OPT_FFMT,
    _OPT_FOPTS,
    _OPT_PIXELRATE,
    _OPT_LIST_MODES,
    _OPT_JSON,
    _OPT_SHUFFLE,
    _OPT_FIT,
    _OPT_MIN_ASPECT,
    _OPT_MAX_ASPECT,
    _OPT_LETTERBOX,
    _OPT_PILLARBOX,
    _OPT_VERSION,
};

static volatile sig_atomic_t _abort = 0;
static volatile sig_atomic_t _isRunning = 0;

void HackTvLib::setLogCallback(LogCallback callback) {
    m_logCallback = std::move(callback);
}

void HackTvLib::log(const char* format, ...) {
    if (m_logCallback) {
        char buffer[1024];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        m_logCallback(buffer);
    }
}

HackTvLib::HackTvLib()
{
    log("HackTvLib members initialized");

#ifdef WIN32
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
#endif

    memset(&s, 0, sizeof(hacktv_t));

    /* Default configuration */
    s.output_type = "hackrf";
    s.output = NULL;
    s.mode = "i";
    s.samplerate = 16000000;
    s.pixelrate = 0;
    s.level = 1.0;
    s.deviation = -1;
    s.gamma = -1;
    s.interlace = 0;
    s.fit_mode = AV_FIT_STRETCH;
    s.repeat = 0;
    s.shuffle = 0;
    s.verbose = 0;
    s.teletext = NULL;
    s.wss = NULL;
    s.videocrypt = NULL;
    s.videocrypt2 = NULL;
    s.videocrypts = NULL;
    s.syster = 0;
    s.systeraudio = 0;
    s.acp = 0;
    s.vits = 0;
    s.vitc = 0;
    s.filter = 0;
    s.nocolour = 0;
    s.noaudio = 0;
    s.nonicam = 0;
    s.a2stereo = 0;
    s.scramble_video = 0;
    s.scramble_audio = 0;
    s.chid = -1;
    s.mac_audio_stereo = MAC_STEREO;
    s.mac_audio_quality = MAC_HIGH_QUALITY;
    s.mac_audio_companded = MAC_COMPANDED;
    s.mac_audio_protection = MAC_FIRST_LEVEL_PROTECTION;
    s.frequency = 0;
    s.amp = 0;
    s.gain = 0;
    s.antenna = NULL;
    s.file_type = RF_INT16;
    s.raw_bb_blanking_level = 0;
    s.raw_bb_white_level = INT16_MAX;
}

HackTvLib::~HackTvLib() {
    stop();
}

void HackTvLib::start(int argc, char *argv[]) {

    log("HackTvLib starting...");

    int option_index;
    static struct option long_options[] = {
        { "output",         required_argument, 0, 'o' },
        { "mode",           required_argument, 0, 'm' },
        { "list-modes",     no_argument,       0, _OPT_LIST_MODES },
        { "samplerate",     required_argument, 0, 's' },
        { "pixelrate",      required_argument, 0, _OPT_PIXELRATE },
        { "level",          required_argument, 0, 'l' },
        { "deviation",      required_argument, 0, 'D' },
        { "gamma",          required_argument, 0, 'G' },
        { "interlace",      no_argument,       0, 'i' },
        { "fit",            required_argument, 0, _OPT_FIT },
        { "min-aspect",     required_argument, 0, _OPT_MIN_ASPECT },
        { "max-aspect",     required_argument, 0, _OPT_MAX_ASPECT },
        { "letterbox",      no_argument,       0, _OPT_LETTERBOX },
        { "pillarbox",      no_argument,       0, _OPT_PILLARBOX },
        { "repeat",         no_argument,       0, 'r' },
        { "shuffle",        no_argument,       0, _OPT_SHUFFLE },
        { "verbose",        no_argument,       0, 'v' },
        { "teletext",       required_argument, 0, _OPT_TELETEXT },
        { "wss",            required_argument, 0, _OPT_WSS },
        { "videocrypt",     required_argument, 0, _OPT_VIDEOCRYPT },
        { "videocrypt2",    required_argument, 0, _OPT_VIDEOCRYPT2 },
        { "videocrypts",    required_argument, 0, _OPT_VIDEOCRYPTS },
        { "syster",         no_argument,       0, _OPT_SYSTER },
        { "systeraudio",    no_argument,       0, _OPT_SYSTERAUDIO },
        { "acp",            no_argument,       0, _OPT_ACP },
        { "vits",           no_argument,       0, _OPT_VITS },
        { "vitc",           no_argument,       0, _OPT_VITC },
        { "filter",         no_argument,       0, _OPT_FILTER },
        { "nocolour",       no_argument,       0, _OPT_NOCOLOUR },
        { "nocolor",        no_argument,       0, _OPT_NOCOLOUR },
        { "noaudio",        no_argument,       0, _OPT_NOAUDIO },
        { "nonicam",        no_argument,       0, _OPT_NONICAM },
        { "a2stereo",       no_argument,       0, _OPT_A2STEREO },
        { "single-cut",     no_argument,       0, _OPT_SINGLE_CUT },
        { "double-cut",     no_argument,       0, _OPT_DOUBLE_CUT },
        { "eurocrypt",      required_argument, 0, _OPT_EUROCRYPT },
        { "scramble-audio", no_argument,       0, _OPT_SCRAMBLE_AUDIO },
        { "chid",           required_argument, 0, _OPT_CHID },
        { "mac-audio-stereo", no_argument,     0, _OPT_MAC_AUDIO_STEREO },
        { "mac-audio-mono", no_argument,       0, _OPT_MAC_AUDIO_MONO },
        { "mac-audio-high-quality", no_argument, 0, _OPT_MAC_AUDIO_HIGH_QUALITY },
        { "mac-audio-medium-quality", no_argument, 0, _OPT_MAC_AUDIO_MEDIUM_QUALITY },
        { "mac-audio-companded", no_argument,  0, _OPT_MAC_AUDIO_COMPANDED },
        { "mac-audio-linear", no_argument,     0, _OPT_MAC_AUDIO_LINEAR },
        { "mac-audio-l1-protection", no_argument, 0, _OPT_MAC_AUDIO_L1_PROTECTION },
        { "mac-audio-l2-protection", no_argument, 0, _OPT_MAC_AUDIO_L2_PROTECTION },
        { "sis",            required_argument, 0, _OPT_SIS },
        { "swap-iq",        no_argument,       0, _OPT_SWAP_IQ },
        { "offset",         required_argument, 0, _OPT_OFFSET },
        { "passthru",       required_argument, 0, _OPT_PASSTHRU },
        { "invert-video",   no_argument,       0, _OPT_INVERT_VIDEO },
        { "raw-bb-file",    required_argument, 0, _OPT_RAW_BB_FILE },
        { "raw-bb-blanking", required_argument, 0, _OPT_RAW_BB_BLANKING },
        { "raw-bb-white",   required_argument, 0, _OPT_RAW_BB_WHITE },
        { "secam-field-id", no_argument,       0, _OPT_SECAM_FIELD_ID },
        { "json",           no_argument,       0, _OPT_JSON },
        { "ffmt",           required_argument, 0, _OPT_FFMT },
        { "fopts",          required_argument, 0, _OPT_FOPTS },
        { "frequency",      required_argument, 0, 'f' },
        { "amp",            no_argument,       0, 'a' },
        { "gain",           required_argument, 0, 'g' },
        { "antenna",        required_argument, 0, 'A' },
        { "type",           required_argument, 0, 't' },
        { "version",        no_argument,       0, _OPT_VERSION },
        { 0,                0,                 0,  0  }
    };

    int c;
    _abort = 0;

    opterr = 0;
    while((c = getopt_long(argc, argv, "o:m:s:D:G:irvf:al:g:A:t:", long_options, &option_index)) != -1)
    {
        switch(c)
        {
        case 'o': /* -o, --output <[type:]target> */

            /* Get a pointer to the output prefix and target */
            pre = optarg;
            sub = strchr(pre, ':');

            if(sub != NULL)
            {
                /* Split the optarg into two */
                *sub = '\0';
                sub++;
            }

            /* Try to match the prefix with a known type */
            if(strcmp(pre, "file") == 0)
            {
                s.output_type = "file";
                s.output = sub;
            }
            else if(strcmp(pre, "hackrf") == 0)
            {
                s.output_type = "hackrf";
                s.output = sub;
            }
            else if(strcmp(pre, "soapysdr") == 0)
            {
                s.output_type = "soapysdr";
                s.output = sub;
            }
            else if(strcmp(pre, "fl2k") == 0)
            {
                s.output_type = "fl2k";
                s.output = sub;
            }
            else
            {
                /* Unrecognised output type, default to file */
                if(sub != NULL)
                {
                    /* Recolonise */
                    sub--;
                    *sub = ':';
                }

                s.output_type = "file";
                s.output = pre;
            }

            break;

        case 'm': /* -m, --mode <name> */
            s.mode = optarg;
            break;

        case _OPT_LIST_MODES: /* --list-modes */
            s.list_modes = 1;
            break;

        case 's': /* -s, --samplerate <value> */
            s.samplerate = atoi(optarg) * 1000000; // Multiply by 1 MHz
            break;

        case _OPT_PIXELRATE: /* --pixelrate <value> */
            s.pixelrate = atoi(optarg);
            break;

        case 'l': /* -l, --level <value> */
            s.level = atof(optarg);
            break;

        case 'D': /* -D, --deviation <value> */
            s.deviation = atof(optarg);
            break;

        case 'G': /* -G, --gamma <value> */
            s.gamma = atof(optarg);
            break;

        case 'i': /* -i, --interlace */
            s.interlace = 1;
            break;

        case _OPT_FIT: /* --fit <mode> */

            if(strcmp(optarg, "stretch") == 0) s.fit_mode = AV_FIT_STRETCH;
            else if(strcmp(optarg, "fill") == 0) s.fit_mode = AV_FIT_FILL;
            else if(strcmp(optarg, "fit") == 0) s.fit_mode = AV_FIT_FIT;
            else if(strcmp(optarg, "none") == 0) s.fit_mode = AV_FIT_NONE;
            else
            {
                fprintf(stderr, "Unrecognised fit mode '%s'.\n", optarg);
                return;
            }

            break;

        case _OPT_MIN_ASPECT: /* --min-aspect <value> */

            if(_parse_ratio(&s.min_aspect, optarg) != HACKTV_OK)
            {
                fprintf(stderr, "Invalid minimum aspect\n");
                return;
            }

            break;

        case _OPT_MAX_ASPECT: /* --max-aspect <value> */

            if(_parse_ratio(&s.max_aspect, optarg) != HACKTV_OK)
            {
                fprintf(stderr, "Invalid maximum aspect\n");
                return;
            }

            break;

        case _OPT_LETTERBOX: /* --letterbox */

            /* For compatiblity with CJ fork */
            s.fit_mode = AV_FIT_FIT;

            break;

        case _OPT_PILLARBOX: /* --pillarbox */

            /* For compatiblity with CJ fork */
            s.fit_mode = AV_FIT_FILL;

            break;

        case 'r': /* -r, --repeat */
            s.repeat = 1;
            break;

        case _OPT_SHUFFLE: /* --shuffle */
            s.shuffle = 1;
            break;

        case 'v': /* -v, --verbose */
            s.verbose = 1;
            break;

        case _OPT_TELETEXT: /* --teletext <path> */
            s.teletext = optarg;
            break;

        case _OPT_WSS: /* --wss <mode> */
            s.wss = optarg;
            break;

        case _OPT_VIDEOCRYPT: /* --videocrypt */
            s.videocrypt = optarg;
            break;

        case _OPT_VIDEOCRYPT2: /* --videocrypt2 */
            s.videocrypt2 = optarg;
            break;

        case _OPT_VIDEOCRYPTS: /* --videocrypts */
            s.videocrypts = optarg;
            break;

        case _OPT_SYSTER: /* --syster */
            s.syster = 1;
            break;

        case _OPT_SYSTERAUDIO: /* --systeraudio */
            s.systeraudio = 1;
            break;

        case _OPT_ACP: /* --acp */
            s.acp = 1;
            break;

        case _OPT_VITS: /* --vits */
            s.vits = 1;
            break;

        case _OPT_VITC: /* --vitc */
            s.vitc = 1;
            break;

        case _OPT_FILTER: /* --filter */
            s.filter = 1;
            break;

        case _OPT_NOCOLOUR: /* --nocolour / --nocolor */
            s.nocolour = 1;
            break;

        case _OPT_NOAUDIO: /* --noaudio */
            s.noaudio = 1;
            break;

        case _OPT_NONICAM: /* --nonicam */
            s.nonicam = 1;
            break;

        case _OPT_A2STEREO: /* --a2stereo */
            s.a2stereo = 1;
            break;

        case _OPT_SINGLE_CUT: /* --single-cut */
            s.scramble_video = 1;
            break;

        case _OPT_DOUBLE_CUT: /* --double-cut */
            s.scramble_video = 2;
            break;

        case _OPT_EUROCRYPT: /* --eurocrypt */
            s.eurocrypt = optarg;
            break;

        case _OPT_SCRAMBLE_AUDIO: /* --scramble-audio */
            s.scramble_audio = 1;
            break;

        case _OPT_CHID: /* --chid <id> */
            s.chid = strtol(optarg, NULL, 0);
            break;

        case _OPT_MAC_AUDIO_STEREO: /* --mac-audio-stereo */
            s.mac_audio_stereo = MAC_STEREO;
            break;

        case _OPT_MAC_AUDIO_MONO: /* --mac-audio-mono */
            s.mac_audio_stereo = MAC_MONO;
            break;

        case _OPT_MAC_AUDIO_HIGH_QUALITY: /* --mac-audio-high-quality */
            s.mac_audio_quality = MAC_HIGH_QUALITY;
            break;

        case _OPT_MAC_AUDIO_MEDIUM_QUALITY: /* --mac-audio-medium-quality */
            s.mac_audio_quality = MAC_MEDIUM_QUALITY;
            break;

        case _OPT_MAC_AUDIO_COMPANDED: /* --mac-audio-companded */
            s.mac_audio_companded = MAC_COMPANDED;
            break;

        case _OPT_MAC_AUDIO_LINEAR: /* --mac-audio-linear */
            s.mac_audio_companded = MAC_LINEAR;
            break;

        case _OPT_MAC_AUDIO_L1_PROTECTION: /* --mac-audio-l1-protection */
            s.mac_audio_protection = MAC_FIRST_LEVEL_PROTECTION;
            break;

        case _OPT_MAC_AUDIO_L2_PROTECTION: /* --mac-audio-l2-protection */
            s.mac_audio_protection = MAC_SECOND_LEVEL_PROTECTION;
            break;

        case _OPT_SIS: /* --sis <mode> */
            s.sis = optarg;
            break;

        case _OPT_SWAP_IQ: /* --swap-iq */
            s.swap_iq = 1;
            break;

        case _OPT_OFFSET: /* --offset <value Hz> */
            s.offset = (int64_t) strtod(optarg, NULL);
            break;

        case _OPT_PASSTHRU: /* --passthru <path> */
            s.passthru = optarg;
            break;

        case _OPT_INVERT_VIDEO: /* --invert-video */
            s.invert_video = 1;
            break;

        case _OPT_RAW_BB_FILE: /* --raw-bb-file <file> */
            s.raw_bb_file = optarg;
            break;

        case _OPT_RAW_BB_BLANKING: /* --raw-bb-blanking <value> */
            s.raw_bb_blanking_level = strtol(optarg, NULL, 0);
            break;

        case _OPT_RAW_BB_WHITE: /* --raw-bb-white <value> */
            s.raw_bb_white_level = strtol(optarg, NULL, 0);
            break;

        case _OPT_SECAM_FIELD_ID: /* --secam-field-id */
            s.secam_field_id = 1;
            break;

        case _OPT_JSON: /* --json */
            s.json = 1;
            break;

        case _OPT_FFMT: /* --ffmt <format> */
            s.ffmt = optarg;
            break;

        case _OPT_FOPTS: /* --fopts <option=value:[option2=value...]> */
            s.fopts = optarg;
            break;

        case 'f': /* -f, --frequency <value> */
            s.frequency = (uint64_t) strtod(optarg, NULL);
            break;

        case 'a': /* -a, --amp */
            s.amp = 1;
            break;

        case 'g': /* -g, --gain <value> */
            s.gain = atoi(optarg);
            break;

        case 'A': /* -A, --antenna <name> */
            s.antenna = optarg;
            break;

        case 't': /* -t, --type <type> */

            if(strcmp(optarg, "uint8") == 0)
            {
                s.file_type = RF_UINT8;
            }
            else if(strcmp(optarg, "int8") == 0)
            {
                s.file_type = RF_INT8;
            }
            else if(strcmp(optarg, "uint16") == 0)
            {
                s.file_type = RF_UINT16;
            }
            else if(strcmp(optarg, "int16") == 0)
            {
                s.file_type = RF_INT16;
            }
            else if(strcmp(optarg, "int32") == 0)
            {
                s.file_type = RF_INT32;
            }
            else if(strcmp(optarg, "float") == 0)
            {
                s.file_type = RF_FLOAT;
            }
            else
            {
                fprintf(stderr, "Unrecognised file data type.\n");
                return;
            }

            break;

        case _OPT_VERSION: /* --version */

            return;

        case '?':
            return;
        }
    }

    if(s.list_modes)
    {
        _list_modes(s.json);
        return;
    }

    if(optind >= argc)
    {
        fprintf(stderr, "No input specified.\n");
        return;
    }

    for(vid_confs = vid_configs; vid_confs->id != NULL; vid_confs++)
    {
        if(strcmp(s.mode, vid_confs->id) == 0) break;
    }

    if(vid_confs->id == NULL)
    {
        fprintf(stderr, "Unrecognised TV mode.\n");
        return;
    }

    memcpy(&vid_conf, vid_confs->conf, sizeof(vid_config_t));

    if(s.deviation > 0)
    {
        /* Override the FM deviation value */
        vid_conf.fm_deviation = s.deviation;
    }

    if(s.gamma > 0)
    {
        /* Override the gamma value */
        vid_conf.gamma = s.gamma;
    }

    if(s.interlace)
    {
        vid_conf.interlace = 1;
    }

    if(s.nocolour)
    {
        if(vid_conf.colour_mode == VID_PAL ||
            vid_conf.colour_mode == VID_SECAM ||
            vid_conf.colour_mode == VID_NTSC)
        {
            vid_conf.colour_mode = VID_NONE;
        }
    }

    if(s.noaudio > 0)
    {
        /* Disable all audio sub-carriers */
        vid_conf.fm_mono_level = 0;
        vid_conf.fm_left_level = 0;
        vid_conf.fm_right_level = 0;
        vid_conf.am_audio_level = 0;
        vid_conf.nicam_level = 0;
        vid_conf.dance_level = 0;
        vid_conf.fm_mono_carrier = 0;
        vid_conf.fm_left_carrier = 0;
        vid_conf.fm_right_carrier = 0;
        vid_conf.nicam_carrier = 0;
        vid_conf.dance_carrier = 0;
        vid_conf.am_mono_carrier = 0;
    }

    if(s.nonicam > 0)
    {
        /* Disable the NICAM sub-carrier */
        vid_conf.nicam_level = 0;
        vid_conf.nicam_carrier = 0;
    }

    if(s.a2stereo > 0)
    {
        vid_conf.a2stereo = 1;
    }

    vid_conf.scramble_video = s.scramble_video;
    vid_conf.scramble_audio = s.scramble_audio;

    vid_conf.level *= s.level;

    if(s.teletext)
    {
        if(vid_conf.lines != 625)
        {
            fprintf(stderr, "Teletext is only available with 625 line modes.\n");
            return;
        }

        vid_conf.teletext = s.teletext;
    }

    if(s.wss)
    {
        if(vid_conf.type != VID_RASTER_625)
        {
            fprintf(stderr, "WSS is only supported for 625 line raster modes.\n");
            return;
        }

        vid_conf.wss = s.wss;
    }

    if(s.videocrypt)
    {
        if(vid_conf.lines != 625 && vid_conf.colour_mode != VID_PAL)
        {
            fprintf(stderr, "Videocrypt I is only compatible with 625 line PAL modes.\n");
            return;
        }

        vid_conf.videocrypt = s.videocrypt;
    }

    if(s.videocrypt2)
    {
        if(vid_conf.lines != 625 && vid_conf.colour_mode != VID_PAL)
        {
            fprintf(stderr, "Videocrypt II is only compatible with 625 line PAL modes.\n");
            return;
        }

        /* Only allow both VC1 and VC2 if both are in free-access mode */
        if(s.videocrypt && !(strcmp(s.videocrypt, "free") == 0 && strcmp(s.videocrypt2, "free") == 0))
        {
            fprintf(stderr, "Videocrypt I and II cannot be used together except in free-access mode.\n");
            return;
        }

        vid_conf.videocrypt2 = s.videocrypt2;
    }

    if(s.videocrypts)
    {
        if(vid_conf.lines != 625 && vid_conf.colour_mode != VID_PAL)
        {
            fprintf(stderr, "Videocrypt S is only compatible with 625 line PAL modes.\n");
            return;
        }

        if(s.videocrypt || s.videocrypt2)
        {
            fprintf(stderr, "Using multiple scrambling modes is not supported.\n");
            return;
        }

        vid_conf.videocrypts = s.videocrypts;
    }

    if(s.syster)
    {
        if(vid_conf.lines != 625 && vid_conf.colour_mode != VID_PAL)
        {
            fprintf(stderr, "Nagravision Syster is only compatible with 625 line PAL modes.\n");
            return;
        }

        if(vid_conf.videocrypt || vid_conf.videocrypt2 || vid_conf.videocrypts)
        {
            fprintf(stderr, "Using multiple scrambling modes is not supported.\n");
            return;
        }

        vid_conf.syster = 1;
        vid_conf.systeraudio = s.systeraudio;
    }

    if(s.eurocrypt)
    {
        if(vid_conf.type != VID_MAC)
        {
            fprintf(stderr, "Eurocrypt is only compatible with D/D2-MAC modes.\n");
            return;
        }

        if(vid_conf.scramble_video == 0)
        {
            /* Default to single-cut scrambling if none was specified */
            vid_conf.scramble_video = 1;
        }

        vid_conf.eurocrypt = s.eurocrypt;
    }

    if(s.acp)
    {
        if(vid_conf.lines != 625 && vid_conf.lines != 525)
        {
            fprintf(stderr, "Analogue Copy Protection is only compatible with 525 and 625 line modes.\n");
            return;
        }

        if(vid_conf.videocrypt || vid_conf.videocrypt2 || vid_conf.videocrypts || vid_conf.syster)
        {
            fprintf(stderr, "Analogue Copy Protection cannot be used with video scrambling enabled.\n");
            return;
        }

        vid_conf.acp = 1;
    }

    if(s.vits)
    {
        if(vid_conf.type != VID_RASTER_625 &&
            vid_conf.type != VID_RASTER_525)
        {
            fprintf(stderr, "VITS is only currently supported for 625 and 525 line raster modes.\n");
            return;
        }

        vid_conf.vits = 1;
    }

    if(s.vitc)
    {
        if(vid_conf.type != VID_RASTER_625 &&
            vid_conf.type != VID_RASTER_525)
        {
            fprintf(stderr, "VITC is only currently supported for 625 and 525 line raster modes.\n");
            return;
        }

        vid_conf.vitc = 1;
    }

    if(vid_conf.type == VID_MAC)
    {
        if(s.chid >= 0)
        {
            vid_conf.chid = (uint16_t) s.chid;
        }

        vid_conf.mac_audio_stereo = s.mac_audio_stereo;
        vid_conf.mac_audio_quality = s.mac_audio_quality;
        vid_conf.mac_audio_protection = s.mac_audio_protection;
        vid_conf.mac_audio_companded = s.mac_audio_companded;
    }

    if(s.filter)
    {
        vid_conf.vfilter = 1;
    }

    if(s.sis)
    {
        if(vid_conf.lines != 625)
        {
            fprintf(stderr, "SiS is only available with 625 line modes.\n");
            return;
        }

        vid_conf.sis = s.sis;
    }

    vid_conf.swap_iq = s.swap_iq;
    vid_conf.offset = s.offset;
    vid_conf.passthru = s.passthru;
    vid_conf.invert_video = s.invert_video;
    vid_conf.raw_bb_file = s.raw_bb_file;
    vid_conf.raw_bb_blanking_level = s.raw_bb_blanking_level;
    vid_conf.raw_bb_white_level = s.raw_bb_white_level;
    vid_conf.secam_field_id = s.secam_field_id;

    log("Output Type: %s", s.output_type);
    log("Frequency: %llu", s.frequency);
    log("Sample Rate: %d", s.samplerate);
    log("Mode: %s", s.mode ? s.mode : "None");

    /* Setup video encoder */
    r = vid_init(&s.vid, s.samplerate, s.pixelrate, &vid_conf);
    if(r != VID_OK)
    {
        fprintf(stderr, "Unable to initialise video encoder.\n");
        return;
    }

    vid_info(&s.vid);

    if(strcmp(s.output_type, "hackrf") == 0)
    {
#ifdef HAVE_HACKRF
        if(rf_hackrf_open(&s.rf, s.output, s.vid.sample_rate, s.frequency, s.gain, s.amp) != RF_OK)
        {
            vid_free(&s.vid);
            log("HackTvLib stoped");
            return;
        }
#else
        fprintf(stderr, "HackRF support is not available in this build of hacktv.\n");
        vid_free(&s.vid);
        return;;
#endif
    }
    else if(strcmp(s.output_type, "soapysdr") == 0)
    {
#ifdef HAVE_SOAPYSDR
        if(rf_soapysdr_open(&s.rf, s.output, s.vid.sample_rate, s.frequency, s.gain, s.antenna) != RF_OK)
        {
            vid_free(&s.vid);
            return;;
        }
#else
        fprintf(stderr, "SoapySDR support is not available in this build of hacktv.\n");
        vid_free(&s.vid);
        return;;
#endif
    }
    else if(strcmp(s.output_type, "fl2k") == 0)
    {
#ifdef HAVE_FL2K
        if(rf_fl2k_open(&s.rf, s.output, s.vid.sample_rate) != RF_OK)
        {
            vid_free(&s.vid);
            return;;
        }
#else
        fprintf(stderr, "FL2K support is not available in this build of hacktv.\n");
        vid_free(&s.vid);
        return;;
#endif
    }
    else if(strcmp(s.output_type, "file") == 0)
    {
        if(rf_file_open(&s.rf, s.output, s.file_type, s.vid.conf.output_type == RF_INT16_COMPLEX) != RF_OK)
        {
            vid_free(&s.vid);
            return;;
        }
    }

    av_ffmpeg_init();

    av_frame_t frame = (av_frame_t) {
        .width = s.vid.active_width,
        .height = s.vid.conf.active_lines,
        .framebuffer = NULL, // Set this to the actual framebuffer if available
        .pixel_stride = 0,  // Initialize with appropriate value if needed
        .line_stride = 0,   // Initialize with appropriate value if needed
        .pixel_aspect_ratio = (rational_t) {
            .num = 1, // Set appropriate numerator
            .den = 1, // Set appropriate denominator
        },
        .interlaced = s.vid.conf.interlace ? 1 : 0, // Set based on interlace flag
    };

    /* Configure AV source settings */
    s.vid.av = (av_t) {
        .width = s.vid.active_width,
        .height = s.vid.conf.active_lines,
        .frame_rate = (rational_t) {
            .num = s.vid.conf.frame_rate.num * (s.vid.conf.interlace ? 2 : 1),
            .den = s.vid.conf.frame_rate.den,
        },
        .display_aspect_ratios = {
            s.vid.conf.frame_aspects[0],
            s.vid.conf.frame_aspects[1]
        },
        .fit_mode = s.fit_mode,
        .min_display_aspect_ratio = s.min_aspect,
        .max_display_aspect_ratio = s.max_aspect,
        .default_frame = frame,
        .frames = 0, // Initialize with a default value if needed
        .sample_rate = (rational_t) {
            .num = (s.vid.audio ? HACKTV_AUDIO_SAMPLE_RATE : 0),
            .den = 1,
        },
        .samples = 0, // Initialize with a default value if needed
        .av_source_ctx = NULL, // Set this to the actual context if available
        .read_video = NULL, // Set these to actual callback functions if available
        .read_audio = NULL,
        .eof = NULL,
        .close = NULL,
    };

    if((s.vid.conf.frame_orientation & 3) == VID_ROTATE_90 ||
        (s.vid.conf.frame_orientation & 3) == VID_ROTATE_270)
    {
        /* Flip dimensions if the lines are scanned vertically */
        s.vid.av.width = s.vid.conf.active_lines;
        s.vid.av.height = s.vid.active_width;
    }

    do
    {
        if(!_isRunning)
            _isRunning = 1;

        if(s.shuffle)
        {
            /* Shuffle the input source list */
            /* Avoids moving the last entry to the start
             * to prevent it repeating immediately */
            for(c = optind; c < argc - 1; c++)
            {
                l = c + (rand() % (argc - c - (c == optind ? 1 : 0)));
                pre = argv[c];
                argv[c] = argv[l];
                argv[l] = pre;
            }
        }

        for(c = optind; c < argc && !_abort; c++)
        {
            /* Get a pointer to the output prefix and target */
            pre = argv[c];
            sub = strchr(pre, ':');

            if(sub != NULL)
            {
                l = sub - pre;
                sub++;
            }
            else
            {
                l = strlen(pre);
            }

            if(strncmp(pre, "test", l) == 0)
            {
                r = av_test_open(&s.vid.av);
            }
            else if(strncmp(pre, "ffmpeg", l) == 0)
            {
                r = av_ffmpeg_open(&s.vid.av, sub, s.ffmt, s.fopts);
            }
            else
            {
                r = av_ffmpeg_open(&s.vid.av, pre, s.ffmt, s.fopts);
            }

            if(r != HACKTV_OK)
            {
                /* Error opening this source. Move to the next */
                continue;
            }

            while(!_abort)
            {
                size_t samples;
                int16_t *data = vid_next_line(&s.vid, &samples);

                if(data == NULL) break;

                if(rf_write(&s.rf, data, samples) != RF_OK) break;
            }

            av_close(&s.vid.av);
        }
    }
    while(s.repeat && !_abort);

    log("HackTvLib stoped");
}

void HackTvLib::stop()
{
    if(_isRunning)
    {
         log("HackTvLib stopping");
        _abort = 1;
    }
    else
        log("HackTvLib already stopped.");
}
