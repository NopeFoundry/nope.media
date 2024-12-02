#include <stdio.h>
#include <stdlib.h>

#include <nopemd.h>

#define FLAG_RESAMPLE (1<<0)

int main(int ac, char **av)
{
    if (ac < 3) {
        fprintf(stderr, "Usage: %s <media.mkv> <flags> [<use_pkt_duration>]\n", av[0]);
        return -1;
    }

    const char *filename = av[1];
    const int flags = atoi(av[2]);
    const int use_pkt_duration = ac > 3 ? atoi(av[3]) : 0;

    int i = 0, ret = 0, smp = 0;
    struct nmd_ctx *s = nmd_create(filename);

    if (!s)
        return -1;

    nmd_set_option(s, "auto_hwaccel", 0);
    nmd_set_option(s, "use_pkt_duration", use_pkt_duration);
    nmd_set_option(s, "avselect", NMD_SELECT_AUDIO);
    nmd_set_option(s, "audio_texture", 0);

    if (flags & FLAG_RESAMPLE) {
         nmd_set_option(s, "filters", "aformat=sample_fmts=flt:sample_rates=48000");
    }

    for (int r = 0; r < 2; r++) {
        printf("run #%d\n", r+1);
        for (;;) {
            struct nmd_frame *frame;

            int ret = nmd_get_next_frame(s, &frame);
            if (ret != NMD_RET_NEWFRAME) {
                printf("null frame\n");
                break;
            }
            printf("frame #%d / data:%p ts:%f nb_samples:%d nmdsmpfmt:%d\n",
                   i++, frame->datap[0], frame->ts, frame->nb_samples, frame->pix_fmt);
            smp += frame->nb_samples;

            nmd_frame_releasep(&frame);
        }
    }

    nmd_freep(&s);

    const int expected_smp = (flags & FLAG_RESAMPLE) ? 17280000 : 15876000;
    if (smp != expected_smp) {
        fprintf(stderr, "decoded %d/%d expected samples\n", smp, expected_smp);
        ret = -1;
    }

    return ret;
}
