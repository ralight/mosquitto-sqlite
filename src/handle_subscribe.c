/*
Copyright (c) 2009-2016 Roger Light <roger@atchoo.org>

All rights reserved. This program and the accompanying materials
are made available under the terms of the Eclipse Public License v1.0
and Eclipse Distribution License v1.0 which accompany this distribution.

The Eclipse Public License is available at
   http://www.eclipse.org/legal/epl-v10.html
and the Eclipse Distribution License is available at
  http://www.eclipse.org/org/documents/edl-v10.php.

Contributors:
   Roger Light - initial implementation and documentation.
*/

#include <stdio.h>
#include <string.h>

#include "config.h"

#include "mosquitto_broker.h"
#include "memory_mosq.h"
#include "packet_mosq.h"
#include "persist_plugin.h"



int handle__subscribe(struct mosquitto_db *db, struct mosquitto *context)
{
	int rc = 0;
	int rc2;
	uint16_t mid;
	char *sub;
	uint8_t qos;
	uint8_t *payload = NULL, *tmp_payload;
	uint32_t payloadlen = 0;
	int len;
	char *sub_mount;

	if(!context) return MOSQ_ERR_INVAL;
	log__printf(NULL, MOSQ_LOG_DEBUG, "Received SUBSCRIBE from %s", context->id);
	/* FIXME - plenty of potential for memory leaks here */

	if(context->protocol == mosq_p_mqtt311){
		if((context->in_packet.command&0x0F) != 0x02){
			return MOSQ_ERR_PROTOCOL;
		}
	}
	if(packet__read_uint16(&context->in_packet, &mid)) return 1;

	while(context->in_packet.pos < context->in_packet.remaining_length){
		sub = NULL;
		if(packet__read_string(&context->in_packet, &sub)){
			mosquitto__free(payload);
			return 1;
		}

		if(sub){
			if(STREMPTY(sub)){
				log__printf(NULL, MOSQ_LOG_INFO, "Empty subscription string from %s, disconnecting.",
					context->address);
				mosquitto__free(sub);
				mosquitto__free(payload);
				return 1;
			}
			if(mosquitto_sub_topic_check(sub)){
				log__printf(NULL, MOSQ_LOG_INFO, "Invalid subscription string from %s, disconnecting.",
					context->address);
				mosquitto__free(sub);
				mosquitto__free(payload);
				return 1;
			}

			if(packet__read_byte(&context->in_packet, &qos)){
				mosquitto__free(sub);
				mosquitto__free(payload);
				return 1;
			}
			if(qos > 2){
				log__printf(NULL, MOSQ_LOG_INFO, "Invalid QoS in subscription command from %s, disconnecting.",
					context->address);
				mosquitto__free(sub);
				mosquitto__free(payload);
				return 1;
			}
			if(context->listener && context->listener->mount_point){
				len = strlen(context->listener->mount_point) + strlen(sub) + 1;
				sub_mount = mosquitto__malloc(len+1);
				if(!sub_mount){
					mosquitto__free(sub);
					mosquitto__free(payload);
					return MOSQ_ERR_NOMEM;
				}
				snprintf(sub_mount, len, "%s%s", context->listener->mount_point, sub);
				sub_mount[len] = '\0';

				mosquitto__free(sub);
				sub = sub_mount;

			}
			log__printf(NULL, MOSQ_LOG_DEBUG, "\t%s (QoS %d)", sub, qos);

#if 0
			/* FIXME
			 * This section has been disabled temporarily. mosquitto_acl_check
			 * calls mosquitto_topic_matches_sub, which can't cope with
			 * checking subscriptions that have wildcards against ACLs that
			 * have wildcards. Bug #1374291 is related.
			 *
			 * It's a very difficult problem when an ACL looks like foo/+/bar
			 * and a subscription request to foo/# is made.
			 *
			 * This should be changed to using MOSQ_ACL_SUBSCRIPTION in the
			 * future anyway.
			 */
			if(context->protocol == mosq_p_mqtt311){
				rc = mosquitto_acl_check(db, context, sub, MOSQ_ACL_READ);
				switch(rc){
					case MOSQ_ERR_SUCCESS:
						break;
					case MOSQ_ERR_ACL_DENIED:
						qos = 0x80;
						break;
					default:
						mosquitto__free(sub);
						return rc;
				}
			}
#endif

			if(qos != 0x80){
#ifdef WITH_PERSISTENCE
				if(!context->clean_session){
					rc2 = persist__sub_add(db, context->id, sub, qos);
					if(rc2){
						rc = rc2;
						/* FIXME */
					}
				}
#endif
				rc2 = sub__add(db, context, sub, qos, &db->subs);
				if(rc2 == MOSQ_ERR_SUCCESS){
					if(sub__retain_queue(db, context, sub, qos)) rc = 1;
				}else if(rc2 != -1){
					rc = rc2;
				}
				log__printf(NULL, MOSQ_LOG_SUBSCRIBE, "%s %d %s", context->id, qos, sub);
			}
			mosquitto__free(sub);

			tmp_payload = mosquitto__realloc(payload, payloadlen + 1);
			if(tmp_payload){
				payload = tmp_payload;
				payload[payloadlen] = qos;
				payloadlen++;
			}else{
				mosquitto__free(payload);

				return MOSQ_ERR_NOMEM;
			}
		}
	}

	if(context->protocol == mosq_p_mqtt311){
		if(payloadlen == 0){
			/* No subscriptions specified, protocol error. */
			return MOSQ_ERR_PROTOCOL;
		}
	}
	if(send__suback(context, mid, payloadlen, payload)) rc = 1;
	mosquitto__free(payload);

#ifdef WITH_PERSISTENCE
	db->persistence_changes++;
#endif

	return rc;
}


