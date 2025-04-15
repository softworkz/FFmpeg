/*
 * copyright (c) 2025 Michael Niedermayer
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <string.h>
#include <stdio.h>

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/mem.h"
#include "libavutil/map.h"


static void print_set(const AVMap *s)
{
    const AVMapEntry *t = NULL;
    while ((t = av_map_iterate(s, t)))
        printf("%s=%s %d,%d   ", t->key, t->value, t->keylen, t->valuelen);
    printf("\n");
}

int main(void)
{
    void *our_cmp[] = {
        strcmp,
        av_map_strcmp_keyvalue,
        av_strcasecmp,
        av_map_supercmp_keyvalue,
        av_map_supercmp_keyvalue,
    };
    void *our_subcmp[] = {
        strcmp,
        strcmp,
        av_strcasecmp,
        av_map_supercmp_key,
        av_strcasecmp,
    };

    for (int settype=0; settype<3; settype++) {
        AVMap *set = av_map_new(our_cmp[settype], NULL, NULL);

        printf("testing empty set\n");

        const AVMapEntry *e = av_map_get(set, "foo", our_subcmp[settype]);
        av_assert0(e == NULL);

        e = av_map_get(set, "foo", our_subcmp[settype]);
        av_assert0(e == NULL);

        int ret = av_map_del(set, "foo", our_subcmp[settype], 0);
        av_assert0(ret == 0);

        print_set(set);

        printf("testing 1-set\n");

        ret = av_map_add(set, "foo", 4, "bar", 4, 0);
        av_assert0(ret == 1);

        ret = av_map_add(set, "foo", 4, "bear", 5, 0);
        av_assert0(ret == ((int[]){0,1,0})[settype]);

        e = av_map_get(set, "foo", our_subcmp[settype]);
        av_assert0(!strcmp(e->key, "foo"));
        if (settype == 1) {
            av_assert0(!strcmp(e->value, "bear") || !strcmp(e->value, "bar"));
        } else {
            av_assert0(!strcmp(e->value, "bar"));
        }

        ret = av_map_add(set, "foo", 4, "bear", 5, AV_MAP_REPLACE);
        av_assert0(ret == 2);

        e = av_map_get(set, "foo", our_subcmp[settype]);
        av_assert0(!strcmp(e->key, "foo"));
        if (settype == 1) {
            av_assert0(!strcmp(e->value, "bear") || !strcmp(e->value, "bar"));
        } else {
            av_assert0(!strcmp(e->value, "bear"));
        }

        e = av_map_get_multiple(set, NULL, "foo", our_subcmp[settype]);
        av_assert0(!strcmp(e->key, "foo"));
        if (settype == 1) {
            av_assert0(!strcmp(e->value, "bar"));
        } else {
            av_assert0(!strcmp(e->value, "bear"));
        }
        e = av_map_get_multiple(set, e, "foo", our_subcmp[settype]);
        if (settype == 1) {
            av_assert0(!strcmp(e->key, "foo"));
            av_assert0(!strcmp(e->value, "bear"));
        } else {
            av_assert0(e == NULL);
        }

        ret = av_map_del(set, "foo", our_subcmp[settype], 0);
        av_assert0(ret == 1);

        e = av_map_get(set, "foo", our_subcmp[settype]);
        if (settype == 1) {
            av_assert0(!strcmp(e->key, "foo"));
            av_assert0(!strcmp(e->value, "bear") || !strcmp(e->value, "bar"));
        } else {
            av_assert0(e == NULL);
        }

        ret = av_map_del(set, "foo", our_subcmp[settype], 0);
        av_assert0(ret == ((int[]){0,1,0})[settype]);


        print_set(set);

        printf("testing n-set\n");
        unsigned r = 5;
        int histogram[256] = {0};
        for(int i=0; i<1000; i++) {
            r = r*123 + 7;
            unsigned char str[3] = {0};
            str[0] = r;
            ret = av_map_add(set, str, 2, str, 2 ,0);
            if (i < 128) {
                if (settype != 2) {
                    av_assert0(ret == 1);
                } else {
                    av_assert0(ret == !histogram[av_toupper(str[0])]);
                    histogram[av_toupper(str[0])] = 1;
                }
            } else {
                av_assert0(ret == 0);
            }
            printf("%d", ret);
        }
        printf("\n");

        r = 5;
        for(int i=0; i<1000; i++) {
            r = r*123 + 7;
            char str[3] = {0};
            str[0] = r;
            e = av_map_get(set, str, our_subcmp[settype]);
            if (settype != 2) {
                av_assert0(!strcmp(e->key, str));
                av_assert0(!strcmp(e->value, str));
            } else {
                av_assert0(!av_strcasecmp(e->key, str));
                av_assert0(!av_strcasecmp(e->value, str));
            }
            e = av_map_get_multiple(set, NULL, str, our_subcmp[settype]);
            if (settype != 2) {
                av_assert0(!strcmp(e->key, str));
                av_assert0(!strcmp(e->value, str));
            } else {
                av_assert0(!av_strcasecmp(e->key, str));
                av_assert0(!av_strcasecmp(e->value, str));
            }
            ret = av_map_add(set, str, 2, str, 2, 0);
            av_assert0(ret == 0);

            str[1]='x';

            e = av_map_get(set, str, our_subcmp[settype]);
            av_assert0(e == NULL);
            e = av_map_get_multiple(set, NULL, str, our_subcmp[settype]);
            av_assert0(e == NULL);
        }
        print_set(set);

        av_map_free(&set);
        av_assert0(!set);
    }

    return 0;
}
