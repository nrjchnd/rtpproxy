/*
 * Copyright (c) 2004-2006 Maxim Sobolev <sobomax@FreeBSD.org>
 * Copyright (c) 2006-2015 Sippy Software, Inc., http://www.sippysoft.com
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>

#include "rtpp_defines.h"
#include "rtpp_log.h"
#include "rtpp_cfg_stable.h"
#include "rtpp_types.h"
#include "rtpp_refcnt.h"
#include "rtpp_weakref.h"
#include "rtpp_hash_table.h"
#include "rtpp_network.h"
#include "rtp.h"
#include "rtp_packet.h"
#include "rtpp_proc.h"
#include "rtpp_proc_servers.h"
#include "rtpp_socket.h"
#include "rtpp_server.h"
#include "rtpp_stream.h"

struct foreach_args {
    double dtime;
    struct sthread_args *sender;
    struct rtpp_proc_rstats *rsp;
    struct rtpp_weakref_obj *rtp_streams_wrt;
    struct rtpp_weakref_obj *rtcp_streams_wrt;
};

static int
process_rtp_servers_foreach(void *dp, void *ap)
{
    struct foreach_args *fap;
    struct rtpp_server *rsrv;
    struct rtp_packet *pkt;
    int len;
    struct rtpp_stream *rsop;
    uint64_t rtps_old;

    fap = (struct foreach_args *)ap;
    /*
     * This method does not need us to bump ref, since we are in the
     * locked context of the rtpp_hash_table, which holds its own ref.
     */
    rsrv = (struct rtpp_server *)dp;
    rsop = CALL_METHOD(fap->rtp_streams_wrt, get_by_idx, rsrv->stuid);
    if (rsop == NULL) {
        return (RTPP_WR_MATCH_CONT);
    }
    for (;;) {
        pkt = CALL_METHOD(rsrv, get, fap->dtime, &len);
        if (pkt == NULL) {
            if (len == RTPS_EOF) {
                struct rtpp_stream *rsop_rtcp;
                CALL_METHOD(rsop, finish_playback, rsrv->sruid);
                rtps_old = rsrv->sruid;
                rsop_rtcp = CALL_METHOD(fap->rtcp_streams_wrt, get_by_idx,
                  rsop->stuid_rtcp);
                if (rsop_rtcp != NULL) {
                    CALL_METHOD(rsop_rtcp, replace_rtps, rtps_old, RTPP_UID_NONE);
                    CALL_METHOD(rsop_rtcp->rcnt, decref);
                }
                CALL_METHOD(rsop->rcnt, decref);
                return (RTPP_WR_MATCH_DEL);
            } else if (len != RTPS_LATER) {
                /* XXX some error, brag to logs */
            }
            break;
        }
        if (rsop->addr == NULL) {
            /* We have a packet, but nowhere to send it, drop */
            rtp_packet_free(pkt);
            continue;
        }
        CALL_METHOD(rsop->fd, send_pkt, fap->sender, rsop->addr,
          SA_LEN(rsop->addr), pkt, rsop->log);
        fap->rsp->npkts_played.cnt++;
    }
    CALL_METHOD(rsop->rcnt, decref);
    return (RTPP_WR_MATCH_CONT);
}

void
rtpp_proc_servers(struct cfg *cf, double dtime, struct sthread_args *sender,
  struct rtpp_proc_rstats *rsp)
{
    struct foreach_args fargs;

    fargs.dtime = dtime;
    fargs.sender = sender;
    fargs.rsp = rsp;
    fargs.rtp_streams_wrt = cf->stable->rtp_streams_wrt;
    fargs.rtcp_streams_wrt = cf->stable->rtcp_streams_wrt;

    CALL_METHOD(cf->stable->servers_wrt, foreach, process_rtp_servers_foreach,
      &fargs);
}
