/*
 * ModSecurity connector for nginx, http://www.modsecurity.org/
 * Copyright (c) 2015 Trustwave Holdings, Inc. (http://www.trustwave.com/)
 *
 * You may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * If any of the files related to licensing are missing or if you have any
 * other questions related to licensing please contact Trustwave Holdings, Inc.
 * directly using the email address security@modsecurity.org.
 *
 */

#ifndef MODSECURITY_DDEBUG
#define MODSECURITY_DDEBUG 0
#endif
#include "ddebug.h"

#include "ngx_http_modsecurity_common.h"

typedef struct {
    ngx_http_request_t *r;
    // ngx_http_core_main_conf_t *cmcf;
    ngx_http_modsecurity_ctx_t *ctx;
    int return_code;
} ngx_http_modsecurity_rewrite_thread_ctx_t;

void ngx_http_modsecurity_rewrite_worker(void *data, ngx_log_t *log)
{
    // ngx_pool_t                   *old_pool;
    ngx_http_modsecurity_rewrite_thread_ctx_t *t_ctx = data;
    ngx_http_modsecurity_ctx_t *ctx = t_ctx->ctx;
    ngx_http_request_t *r = t_ctx->r;

    ngx_log_error(NGX_LOG_DEBUG, log, 0, "[ModSecurity] Rewrite Job Dispatched");

    /*
     * FIXME:
     * In order to perform some tests, let's accept everything.
     *
    if (r->method != NGX_HTTP_GET &&
        r->method != NGX_HTTP_POST && r->method != NGX_HTTP_HEAD) {
        dd("ModSecurity is not ready to deal with anything different from " \
            "POST, GET or HEAD");
        return NGX_DECLINED;
    }
    */

    if (ctx == NULL)
    {
        int ret = 0;

        ngx_connection_t *connection = r->connection;
        /**
         * FIXME: We may want to use struct sockaddr instead of addr_text.
         *
         */
        ngx_str_t addr_text = connection->addr_text;

        t_ctx->ctx = ngx_http_modsecurity_create_ctx(r);
        ctx = t_ctx->ctx;

        ngx_log_error(NGX_LOG_DEBUG, log, 0, "ctx was NULL, creating new context: %p", ctx);

        if (ctx == NULL)
        {
            ngx_log_error(NGX_LOG_ERR, log, 0, "ctx still null; Nothing we can do, returning an error.");
            t_ctx->return_code = NGX_HTTP_INTERNAL_SERVER_ERROR;
            return;
        }

        /**
         * FIXME: Check if it is possible to hook on nginx on a earlier phase.
         *
         * At this point we are doing an late connection process. Maybe
         * we have to hook into NGX_HTTP_FIND_CONFIG_PHASE, it seems to be the
         * erliest phase that nginx allow us to attach those kind of hooks.
         *
         */
        int client_port = ngx_inet_get_port(connection->sockaddr);
        int server_port = ngx_inet_get_port(connection->local_sockaddr);

        const char *client_addr = ngx_str_to_char(addr_text, r->pool);
        if (client_addr == (char *)-1)
        {
            t_ctx->return_code = NGX_HTTP_INTERNAL_SERVER_ERROR;
            return;
        }

        ngx_str_t s;
        u_char addr[NGX_SOCKADDR_STRLEN];
        s.len = NGX_SOCKADDR_STRLEN;
        s.data = addr;
        if (ngx_connection_local_sockaddr(r->connection, &s, 0) != NGX_OK)
        {
            t_ctx->return_code = NGX_HTTP_INTERNAL_SERVER_ERROR;
            return;
        }

        const char *server_addr = ngx_str_to_char(s, r->pool);
        if (server_addr == (char *)-1)
        {
            t_ctx->return_code = NGX_HTTP_INTERNAL_SERVER_ERROR;
            return;
        }

        // old_pool = ngx_http_modsecurity_pcre_malloc_init(r->pool);
        ret = msc_process_connection(ctx->modsec_transaction,
                                     client_addr, client_port,
                                     server_addr, server_port);
        // ngx_http_modsecurity_pcre_malloc_done(old_pool);
        if (ret != 1)
        {
            ngx_log_error(NGX_LOG_WARN, log, 0, "Was not able to extract connection information.");
        }
        /**
         *
         * FIXME: Check how we can finalize a request without crash nginx.
         *
         * I don't think nginx is expecting to finalize a request at that
         * point as it seems that it clean the ngx_http_request_t information
         * and try to use it later.
         *
         */
        ngx_log_error(NGX_LOG_DEBUG, log, 0, "Processing intervention with the connection information filled in");
        ret = ngx_http_modsecurity_process_intervention(ctx->modsec_transaction, r, 1);
        if (ret > 0)
        {
            ctx->intervention_triggered = 1;
            t_ctx->return_code = ret;
            return;
        }

        const char *http_version;
        switch (r->http_version)
        {
        case NGX_HTTP_VERSION_9:
            http_version = "0.9";
            break;
        case NGX_HTTP_VERSION_10:
            http_version = "1.0";
            break;
        case NGX_HTTP_VERSION_11:
            http_version = "1.1";
            break;
#if defined(nginx_version) && nginx_version >= 1009005
        case NGX_HTTP_VERSION_20:
            http_version = "2.0";
            break;
#endif
        default:
            http_version = ngx_str_to_char(r->http_protocol, r->pool);
            if (http_version == (char *)-1)
            {
                t_ctx->return_code = NGX_HTTP_INTERNAL_SERVER_ERROR;
                return;
            }
            if ((http_version != NULL) && (strlen(http_version) > 5) && (!strncmp("HTTP/", http_version, 5)))
            {
                http_version += 5;
            }
            else
            {
                http_version = "1.0";
            }
            break;
        }

        const char *n_uri = ngx_str_to_char(r->unparsed_uri, r->pool);
        const char *n_method = ngx_str_to_char(r->method_name, r->pool);
        if (n_uri == (char *)-1 || n_method == (char *)-1)
        {
            t_ctx->return_code = NGX_HTTP_INTERNAL_SERVER_ERROR;
            return;
        }
        if (n_uri == NULL)
        {
            ngx_log_error(NGX_LOG_ERR, log, 0, "uri is of length zero");
            t_ctx->return_code = NGX_HTTP_INTERNAL_SERVER_ERROR;
            return;
        }
        // old_pool = ngx_http_modsecurity_pcre_malloc_init(r->pool);
        msc_process_uri(ctx->modsec_transaction, n_uri, n_method, http_version);
        // ngx_http_modsecurity_pcre_malloc_done(old_pool);

        ngx_log_error(NGX_LOG_DEBUG, log, 0, "Processing intervention with the transaction information filled in (uri, method and version)");
        ret = ngx_http_modsecurity_process_intervention(ctx->modsec_transaction, r, 1);
        if (ret > 0)
        {
            ctx->intervention_triggered = 1;
            t_ctx->return_code = ret;
            return;
        }

        /**
         * Since incoming request headers are already in place, lets send it to ModSecurity
         *
         */
        ngx_list_part_t *part = &r->headers_in.headers.part;
        ngx_table_elt_t *data = part->elts;
        ngx_uint_t i = 0;
        for (i = 0; /* void */; i++)
        {
            if (i >= part->nelts)
            {
                if (part->next == NULL)
                {
                    break;
                }

                part = part->next;
                data = part->elts;
                i = 0;
            }

            /**
             * By using u_char (utf8_t) I believe nginx is hoping to deal
             * with utf8 strings.
             * Casting those into to unsigned char * in order to pass
             * it to ModSecurity, it will handle with those later.
             *
             */

            ngx_log_error(NGX_LOG_DEBUG, log, 0, "Adding request header: %.*s with value %.*s", (int)data[i].key.len, data[i].key.data, (int)data[i].value.len, data[i].value.data);
            msc_add_n_request_header(ctx->modsec_transaction,
                                     (const unsigned char *)data[i].key.data,
                                     data[i].key.len,
                                     (const unsigned char *)data[i].value.data,
                                     data[i].value.len);
        }

        /**
         * Since ModSecurity already knew about all headers, i guess it is safe
         * to process this information.
         */

        // old_pool = ngx_http_modsecurity_pcre_malloc_init(r->pool);
        msc_process_request_headers(ctx->modsec_transaction);
        // ngx_http_modsecurity_pcre_malloc_done(old_pool);
        ngx_log_error(NGX_LOG_DEBUG, log, 0, "Processing intervention with the request headers information filled in");
        ret = ngx_http_modsecurity_process_intervention(ctx->modsec_transaction, r, 1);
        // if (r->error_page)
        // {
        //     return NGX_DECLINED;
        // }
        if (!r->error_page && ret > 0)
        {
            ctx->intervention_triggered = 1;
            t_ctx->return_code = ret;
            return;
        }
    }

    t_ctx->return_code = NGX_DECLINED;
    return;
}

void ngx_http_modsecurity_rewrite_finalizer(ngx_event_t *ev)
{
    ngx_http_modsecurity_rewrite_thread_ctx_t *ctx = ev->data;
    ngx_http_core_main_conf_t *cmcf;

    ngx_log_error(NGX_LOG_DEBUG, ctx->r->connection->log, 0, "[ModSecurity] Rewrite Job Finalized");

    --ctx->r->main->blocked; /* incremented in ngx_http_modsecurity_prevention_task_offload */
    ctx->r->aio = 0;

    cmcf = ngx_http_get_module_main_conf(ctx->r, ngx_http_core_module);

    switch (ctx->return_code)
    {
        case NGX_OK:
            ctx->r->phase_handler = cmcf->phase_engine.handlers->next;
            ngx_http_core_run_phases(ctx->r);
            break;
        case NGX_DECLINED:
            ctx->r->phase_handler++;
            ngx_http_core_run_phases(ctx->r);
            break;
        case NGX_AGAIN:
        case NGX_DONE:
            // ngx_http_core_run_phases(ctx->r);
            break;
        default:
            ngx_http_discard_request_body(ctx->r);
            ngx_http_finalize_request(ctx->r, ctx->return_code);
    }

    ngx_http_run_posted_requests(ctx->r->connection);
}

ngx_int_t ngx_http_modsecurity_rewrite_handler(ngx_http_request_t *r)
{
    ngx_http_modsecurity_conf_t *mcf;
    ngx_http_modsecurity_rewrite_thread_ctx_t *ctx;
    ngx_http_modsecurity_ctx_t *m_ctx;
    ngx_thread_task_t *task;

    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "catching a new _rewrite_ phase handler");

    mcf = ngx_http_get_module_loc_conf(r, ngx_http_modsecurity_module);
    if (mcf == NULL || mcf->enable != 1)
    {
        ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "ModSecurity not enabled... returning");
        return NGX_DECLINED;
    }

    m_ctx = ngx_http_get_module_ctx(r, ngx_http_modsecurity_module);

    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "recovering ctx: %p", m_ctx);

    task = ngx_thread_task_alloc(r->pool, sizeof(ngx_http_modsecurity_rewrite_thread_ctx_t));

    ctx = task->ctx;
    ctx->r = r;
    ctx->ctx = m_ctx;
    ctx->return_code = NGX_DECLINED;

    task->handler = ngx_http_modsecurity_rewrite_worker;
    task->event.handler = ngx_http_modsecurity_rewrite_finalizer;
    task->event.data = ctx;

    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "[ModSecurity] Using Thread Pool: %p", mcf->thread_pool);

    if (ngx_thread_task_post(mcf->thread_pool, task) != NGX_OK)
    {
        return NGX_ERROR;
    }

    r->main->blocked++;
    r->aio = 1;

    return NGX_DONE;
}
