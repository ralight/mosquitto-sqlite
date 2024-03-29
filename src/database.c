/*
Copyright (c) 2009-2015 Roger Light <roger@atchoo.org>

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

#include <assert.h>
#include <stdio.h>

#include "config.h"

#include "mosquitto_broker.h"
#include "memory_mosq.h"
#include "persist_plugin.h"
#include "send_mosq.h"
#include "sys_tree.h"
#include "time_mosq.h"

static int max_inflight = 20;
static int max_queued = 100;

int db__open(struct mosquitto__config *config, struct mosquitto_db *db)
{
	int rc = 0;
	struct mosquitto__subhier *child;

	if(!config || !db) return MOSQ_ERR_INVAL;

	db->last_db_id = 0;

	db->contexts_by_id = NULL;
	db->contexts_by_sock = NULL;
	db->contexts_for_free = NULL;
#ifdef WITH_BRIDGE
	db->bridges = NULL;
	db->bridge_count = 0;
#endif

	// Initialize the hashtable
	db->clientid_index_hash = NULL;

	db->subs.next = NULL;
	db->subs.subs = NULL;
	db->subs.topic_len = strlen("");
	if(UHPA_ALLOC(db->subs.topic, db->subs.topic_len+1) == 0){
		db->subs.topic_len = 0;
		log__printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory.");
		return MOSQ_ERR_NOMEM;
	}else{
		strncpy(UHPA_ACCESS(db->subs.topic, db->subs.topic_len+1), "", db->subs.topic_len+1);
	}

	child = mosquitto__malloc(sizeof(struct mosquitto__subhier));
	if(!child){
		log__printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory.");
		return MOSQ_ERR_NOMEM;
	}
	child->parent = NULL;
	child->next = NULL;
	child->topic_len = strlen("");
	if(UHPA_ALLOC_TOPIC(child) == 0){
		child->topic_len = 0;
		log__printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory.");
		return MOSQ_ERR_NOMEM;
	}else{
		strncpy(UHPA_ACCESS_TOPIC(child), "", child->topic_len+1);
	}
	child->subs = NULL;
	child->children = NULL;
	child->retained = NULL;
	db->subs.children = child;

	child = mosquitto__malloc(sizeof(struct mosquitto__subhier));
	if(!child){
		log__printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory.");
		return MOSQ_ERR_NOMEM;
	}
	child->parent = NULL;
	child->next = NULL;
	child->topic_len = strlen("$SYS");
	if(UHPA_ALLOC_TOPIC(child) == 0){
		child->topic_len = 0;
		log__printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory.");
		return MOSQ_ERR_NOMEM;
	}else{
		strncpy(UHPA_ACCESS_TOPIC(child), "$SYS", child->topic_len+1);
	}
	child->subs = NULL;
	child->children = NULL;
	child->retained = NULL;
	db->subs.children->next = child;

	db->unpwd = NULL;

#ifdef WITH_PERSISTENCE
	if(config->persistence && config->persistence_filepath){
		if(config->persistence_plugin){
			if(persist__plugin_restore(db)){
				/* FIXME - log error */
				return 1;
			}
		}else{
			if(persist__restore(db)) return 1;
		}
	}
#endif

	return rc;
}

static void subhier_clean(struct mosquitto_db *db, struct mosquitto__subhier *subhier)
{
	struct mosquitto__subhier *next;
	struct mosquitto__subleaf *leaf, *nextleaf;

	while(subhier){
		next = subhier->next;
		leaf = subhier->subs;
		while(leaf){
			nextleaf = leaf->next;
			mosquitto__free(leaf);
			leaf = nextleaf;
		}
		if(subhier->retained){
			db__msg_store_deref(db, &subhier->retained);
		}
		subhier_clean(db, subhier->children);
		UHPA_FREE_TOPIC(subhier);

		mosquitto__free(subhier);
		subhier = next;
	}
}

int db__close(struct mosquitto_db *db)
{
	subhier_clean(db, db->subs.children);
	db__msg_store_clean(db);

	return MOSQ_ERR_SUCCESS;
}


void db__msg_store_add(struct mosquitto_db *db, struct mosquitto_msg_store *store)
{
	store->next = db->msg_store;
	store->prev = NULL;
	if(db->msg_store){
		db->msg_store->prev = store;
	}
	db->msg_store = store;
}


void db__msg_store_remove(struct mosquitto_db *db, struct mosquitto_msg_store *store)
{
	int i;

	if(store->persisted){
		persist__msg_store_delete(db, store);
	}

	if(store->prev){
		store->prev->next = store->next;
		if(store->next){
			store->next->prev = store->prev;
		}
	}else{
		db->msg_store = store->next;
		if(store->next){
			store->next->prev = NULL;
		}
	}
	db->msg_store_count--;

	mosquitto__free(store->source_id);
	if(store->dest_ids){
		for(i=0; i<store->dest_id_count; i++){
			mosquitto__free(store->dest_ids[i]);
		}
		mosquitto__free(store->dest_ids);
	}
	mosquitto__free(store->topic);
	UHPA_FREE_PAYLOAD(store);
	mosquitto__free(store);
}


void db__msg_store_clean(struct mosquitto_db *db)
{
	struct mosquitto_msg_store *store, *next;;

	store = db->msg_store;
	while(store){
		next = store->next;
		db__msg_store_remove(db, store);
		store = next;
	}
}

void db__msg_store_deref(struct mosquitto_db *db, struct mosquitto_msg_store **store)
{
	(*store)->ref_count--;
	if((*store)->ref_count == 0){
		db__msg_store_remove(db, *store);
		*store = NULL;
	}
}


static void db__message_remove(struct mosquitto_db *db, struct mosquitto *context, struct mosquitto_client_msg **msg, struct mosquitto_client_msg *last)
{
	int i;
	struct mosquitto_client_msg *tail;

	if(!context || !msg || !(*msg)){
		return;
	}

#ifdef WITH_PERSISTENCE
	/* FIXME */ persist__client_msg_delete(db, context->id, (*msg)->mid, (*msg)->direction);
#endif
	if((*msg)->store){
		db__msg_store_deref(db, &(*msg)->store);
	}
	if(last){
		last->next = (*msg)->next;
		if(!last->next){
			context->last_msg = last;
		}
	}else{
		context->msgs = (*msg)->next;
		if(!context->msgs){
			context->last_msg = NULL;
		}
	}
	context->msg_count--;
	if((*msg)->qos > 0){
		context->msg_count12--;
	}
	mosquitto__free(*msg);
	if(last){
		*msg = last->next;
	}else{
		*msg = context->msgs;
	}
	tail = context->msgs;
	i = 0;
	while(tail && tail->state == mosq_ms_queued && i<max_inflight){
		if(tail->direction == mosq_md_out){
			switch(tail->qos){
				case 0:
					db__client_msg_state_set(db, context, tail, mosq_ms_publish_qos0);
					break;
				case 1:
					db__client_msg_state_set(db, context, tail, mosq_ms_publish_qos1);
					break;
				case 2:
					db__client_msg_state_set(db, context, tail, mosq_ms_publish_qos2);
					break;
			}
		}else{
			if(tail->qos == 2){
				db__client_msg_state_set(db, context, tail, mosq_ms_send_pubrec);
			}
		}

		tail = tail->next;
	}
}


int db__message_delete(struct mosquitto_db *db, struct mosquitto *context, uint16_t mid, enum mosquitto_msg_direction dir)
{
	struct mosquitto_client_msg *tail, *last = NULL;
	int msg_index = 0;
	bool deleted = false;

	if(!context) return MOSQ_ERR_INVAL;

	tail = context->msgs;
	while(tail){
		msg_index++;

		if(tail->state == mosq_ms_queued && msg_index <= max_inflight){
			tail->timestamp = mosquitto_time();
			if(tail->direction == mosq_md_out){
				switch(tail->qos){
					case 0:
						db__client_msg_state_set(db, context, tail, mosq_ms_publish_qos0);
						break;
					case 1:
						db__client_msg_state_set(db, context, tail, mosq_ms_publish_qos1);
						break;
					case 2:
						db__client_msg_state_set(db, context, tail, mosq_ms_publish_qos2);
						break;
				}
			}else{
				if(tail->qos == 2){
					db__client_msg_state_set(db, context, tail, mosq_ms_wait_for_pubrel);
				}
			}
		}

		if(tail->mid == mid && tail->direction == dir){
			msg_index--;
			db__message_remove(db, context, &tail, last);
			deleted = true;
		}else{
			last = tail;
			tail = tail->next;
		}
		if(msg_index > max_inflight && deleted){
			return MOSQ_ERR_SUCCESS;
		}
	}

	return MOSQ_ERR_SUCCESS;
}

int db__message_insert(struct mosquitto_db *db, struct mosquitto *context, uint16_t mid, enum mosquitto_msg_direction dir, int qos, bool retain, struct mosquitto_msg_store *stored)
{
	struct mosquitto_client_msg *msg = NULL;
	enum mosquitto_msg_state state = mosq_ms_invalid;
	int rc = 0;
	int i;
	char **dest_ids;

	assert(stored);
	if(!context) return MOSQ_ERR_INVAL;
	if(!context->id) return MOSQ_ERR_SUCCESS; /* Protect against unlikely "client is disconnected but not entirely freed" scenario */

	/* Check whether we've already sent this message to this client
	 * for outgoing messages only.
	 * If retain==true then this is a stale retained message and so should be
	 * sent regardless. FIXME - this does mean retained messages will received
	 * multiple times for overlapping subscriptions, although this is only the
	 * case for SUBSCRIPTION with multiple subs in so is a minor concern.
	 */
	if(db->config->allow_duplicate_messages == false
			&& dir == mosq_md_out && retain == false && stored->dest_ids){

		for(i=0; i<stored->dest_id_count; i++){
			if(!strcmp(stored->dest_ids[i], context->id)){
				/* We have already sent this message to this client. */
				return MOSQ_ERR_SUCCESS;
			}
		}
	}
	if(context->sock == INVALID_SOCKET){
		/* Client is not connected only queue messages with QoS>0. */
		if(qos == 0 && !db->config->queue_qos0_messages){
			if(!context->bridge){
				return 2;
			}else{
				if(context->bridge->start_type != bst_lazy){
					return 2;
				}
			}
		}
	}

	if(context->sock != INVALID_SOCKET){
		if(qos == 0 || max_inflight == 0 || context->msg_count12 < max_inflight){
			if(dir == mosq_md_out){
				switch(qos){
					case 0:
						state = mosq_ms_publish_qos0;
						break;
					case 1:
						state = mosq_ms_publish_qos1;
						break;
					case 2:
						state = mosq_ms_publish_qos2;
						break;
				}
			}else{
				if(qos == 2){
					state = mosq_ms_wait_for_pubrel;
				}else{
					return 1;
				}
			}
		}else if(max_queued == 0 || context->msg_count12-max_inflight < max_queued){
			state = mosq_ms_queued;
			rc = 2;
		}else{
			/* Dropping message due to full queue. */
			if(context->is_dropping == false){
				context->is_dropping = true;
				log__printf(NULL, MOSQ_LOG_NOTICE,
						"Outgoing messages are being dropped for client %s.",
						context->id);
			}
			G_MSGS_DROPPED_INC();
			return 2;
		}
	}else{
		if(max_queued > 0 && context->msg_count12 >= max_queued){
			G_MSGS_DROPPED_INC();
			if(context->is_dropping == false){
				context->is_dropping = true;
				log__printf(NULL, MOSQ_LOG_NOTICE,
						"Outgoing messages are being dropped for client %s.",
						context->id);
			}
			return 2;
		}else{
			state = mosq_ms_queued;
		}
	}
	assert(state != mosq_ms_invalid);

#ifdef WITH_PERSISTENCE
	if(state == mosq_ms_queued){
		db->persistence_changes++;
	}
	if(!context->clean_session){
		/* FIXME */ persist__client_msg_add(db, context->id,  stored, mid, qos, retain, dir, state, false);
	}
#endif

	msg = mosquitto__malloc(sizeof(struct mosquitto_client_msg));
	if(!msg) return MOSQ_ERR_NOMEM;
	msg->next = NULL;
	msg->store = stored;
	msg->store->ref_count++;
	msg->mid = mid;
	msg->timestamp = mosquitto_time();
	msg->direction = dir;
	msg->state = state;
	msg->dup = false;
	msg->qos = qos;
	msg->retain = retain;
	if(context->last_msg){
		context->last_msg->next = msg;
		context->last_msg = msg;
	}else{
		context->msgs = msg;
		context->last_msg = msg;
	}
	context->msg_count++;
	if(qos > 0){
		context->msg_count12++;
	}

	if(db->config->allow_duplicate_messages == false && dir == mosq_md_out && retain == false){
		/* Record which client ids this message has been sent to so we can avoid duplicates.
		 * Outgoing messages only.
		 * If retain==true then this is a stale retained message and so should be
		 * sent regardless. FIXME - this does mean retained messages will received
		 * multiple times for overlapping subscriptions, although this is only the
		 * case for SUBSCRIPTION with multiple subs in so is a minor concern.
		 */
		dest_ids = mosquitto__realloc(stored->dest_ids, sizeof(char *)*(stored->dest_id_count+1));
		if(dest_ids){
			stored->dest_ids = dest_ids;
			stored->dest_id_count++;
			stored->dest_ids[stored->dest_id_count-1] = mosquitto__strdup(context->id);
			if(!stored->dest_ids[stored->dest_id_count-1]){
				return MOSQ_ERR_NOMEM;
			}
		}else{
			return MOSQ_ERR_NOMEM;
		}
	}
#ifdef WITH_BRIDGE
	if(context->bridge && context->bridge->start_type == bst_lazy
			&& context->sock == INVALID_SOCKET
			&& context->msg_count >= context->bridge->threshold){

		context->bridge->lazy_reconnect = true;
	}
#endif

#ifdef WITH_WEBSOCKETS
	if(context->wsi && rc == 0){
		return db__message_write(db, context);
	}else{
		return rc;
	}
#else
	return rc;
#endif
}

int db__message_update(struct mosquitto_db *db, struct mosquitto *context, uint16_t mid, enum mosquitto_msg_direction dir, enum mosquitto_msg_state state)
{
	struct mosquitto_client_msg *tail;

	tail = context->msgs;
	while(tail){
		if(tail->mid == mid && tail->direction == dir){
			tail->timestamp = mosquitto_time();
			db__client_msg_state_set(db, context, tail, state);
			return MOSQ_ERR_SUCCESS;
		}
		tail = tail->next;
	}
	return 1;
}

int db__messages_delete(struct mosquitto_db *db, struct mosquitto *context)
{
	struct mosquitto_client_msg *tail, *next;

	if(!context) return MOSQ_ERR_INVAL;

	tail = context->msgs;
	while(tail){
#ifdef WITH_PERSISTENCE
		/* FIXME */ persist__client_msg_delete(db, context->id, tail->mid, tail->direction);
#endif
		db__msg_store_deref(db, &tail->store);
		next = tail->next;
		mosquitto__free(tail);
		tail = next;
	}
	context->msgs = NULL;
	context->last_msg = NULL;
	context->msg_count = 0;
	context->msg_count12 = 0;

	return MOSQ_ERR_SUCCESS;
}

int db__messages_easy_queue(struct mosquitto_db *db, struct mosquitto *context, const char *topic, int qos, uint32_t payloadlen, const void *payload, int retain)
{
	struct mosquitto_msg_store *stored;
	char *source_id;
	char *topic_heap;
	mosquitto__payload_uhpa payload_uhpa;

	assert(db);

	payload_uhpa.ptr = NULL;

	if(!topic) return MOSQ_ERR_INVAL;
	topic_heap = mosquitto__strdup(topic);
	if(!topic_heap) return MOSQ_ERR_INVAL;

	if(UHPA_ALLOC(payload_uhpa, payloadlen) == 0){
		mosquitto__free(topic_heap);
		return MOSQ_ERR_NOMEM;
	}
	memcpy(UHPA_ACCESS(payload_uhpa, payloadlen), payload, payloadlen);

	if(context && context->id){
		source_id = context->id;
	}else{
		source_id = "";
	}
	if(db__message_store(db, source_id, 0, topic_heap, qos, payloadlen, &payload_uhpa, retain, &stored, 0)) return 1;

	return sub__messages_queue(db, source_id, topic_heap, qos, retain, &stored, true);
}

/* This function requires topic to be allocated on the heap. Once called, it owns topic and will free it on error. Likewise payload. */
int db__message_store(struct mosquitto_db *db, const char *source, uint16_t source_mid, char *topic, int qos, uint32_t payloadlen, mosquitto__payload_uhpa *payload, int retain, struct mosquitto_msg_store **stored, dbid_t store_id)
{
	struct mosquitto_msg_store *temp = NULL;
	int rc = MOSQ_ERR_SUCCESS;

	assert(db);
	assert(stored);

	temp = mosquitto__malloc(sizeof(struct mosquitto_msg_store));
	if(!temp){
		log__printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory.");
		rc = MOSQ_ERR_NOMEM;
		goto error;
	}

	temp->persisted = false;
	temp->topic = NULL;
	temp->payload.ptr = NULL;

	temp->ref_count = 0;
	if(source){
		temp->source_id = mosquitto__strdup(source);
	}else{
		temp->source_id = mosquitto__strdup("");
	}
	if(!temp->source_id){
		log__printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory.");
		rc = MOSQ_ERR_NOMEM;
		goto error;
	}
	temp->source_mid = source_mid;
	temp->mid = 0;
	temp->qos = qos;
	temp->retain = retain;
	temp->topic = topic;
	topic = NULL;
	temp->payloadlen = payloadlen;
	if(payloadlen){
		UHPA_MOVE(temp->payload, *payload, payloadlen);
	}else{
		temp->payload.ptr = NULL;
	}

	temp->dest_ids = NULL;
	temp->dest_id_count = 0;
	db->msg_store_count++;
	(*stored) = temp;

	if(!store_id){
		temp->db_id = ++db->last_db_id;
	}else{
		temp->db_id = store_id;
	}

	db__msg_store_add(db, temp);

	return MOSQ_ERR_SUCCESS;
error:
	mosquitto__free(topic);
	if(temp){
		mosquitto__free(temp->source_id);
		mosquitto__free(temp->topic);
		mosquitto__free(temp);
	}
	return rc;
}

int db__message_store_find(struct mosquitto *context, uint16_t mid, struct mosquitto_msg_store **stored)
{
	struct mosquitto_client_msg *tail;

	if(!context) return MOSQ_ERR_INVAL;

	*stored = NULL;
	tail = context->msgs;
	while(tail){
		if(tail->store->source_mid == mid && tail->direction == mosq_md_in){
			*stored = tail->store;
			return MOSQ_ERR_SUCCESS;
		}
		tail = tail->next;
	}

	return 1;
}

/* Called on reconnect to set outgoing messages to a sensible state and force a
 * retry, and to set incoming messages to expect an appropriate retry. */
int db__message_reconnect_reset(struct mosquitto_db *db, struct mosquitto *context)
{
	struct mosquitto_client_msg *msg;
	struct mosquitto_client_msg *prev = NULL;
	int count;

	msg = context->msgs;
	context->msg_count = 0;
	context->msg_count12 = 0;
	while(msg){
		context->last_msg = msg;

		context->msg_count++;
		if(msg->qos > 0){
			context->msg_count12++;
		}

		if(msg->direction == mosq_md_out){
			if(msg->state != mosq_ms_queued){
				switch(msg->qos){
					case 0:
						db__client_msg_state_set(db, context, msg, mosq_ms_publish_qos0);
						break;
					case 1:
						db__client_msg_state_set(db, context, msg, mosq_ms_publish_qos1);
						break;
					case 2:
						if(msg->state == mosq_ms_wait_for_pubcomp){
							db__client_msg_state_set(db, context, msg, mosq_ms_resend_pubrel);
						}else{
							db__client_msg_state_set(db, context, msg, mosq_ms_publish_qos2);
						}
						break;
				}
			}
			/* FIXME */ persist__client_msg_update(db, context->id, msg-> mid, msg->direction, msg->state, msg->dup);
		}else{
			if(msg->qos != 2){
				/* Anything <QoS 2 can be completely retried by the client at
				 * no harm. */
				db__message_remove(db, context, &msg, prev);
			}else{
				/* Message state can be preserved here because it should match
				 * whatever the client has got. */
			}
		}
		prev = msg;
		if(msg) msg = msg->next;
	}
	/* Messages received when the client was disconnected are put
	 * in the mosq_ms_queued state. If we don't change them to the
	 * appropriate "publish" state, then the queued messages won't
	 * get sent until the client next receives a message - and they
	 * will be sent out of order.
	 */
	if(context->msgs){
		count = 0;
		msg = context->msgs;
		while(msg && (max_inflight == 0 || count < max_inflight)){
			if(msg->state == mosq_ms_queued){
				switch(msg->qos){
					case 0:
						db__client_msg_state_set(db, context, msg, mosq_ms_publish_qos0);
						break;
					case 1:
						db__client_msg_state_set(db, context, msg, mosq_ms_publish_qos1);
						break;
					case 2:
						db__client_msg_state_set(db, context, msg, mosq_ms_publish_qos2);
						break;
				}
			}
			msg = msg->next;
			count++;
		}
	}

	return MOSQ_ERR_SUCCESS;
}


int db__message_release(struct mosquitto_db *db, struct mosquitto *context, uint16_t mid, enum mosquitto_msg_direction dir)
{
	struct mosquitto_client_msg *tail, *last = NULL;
	int qos;
	int retain;
	char *topic;
	char *source_id;
	int msg_index = 0;
	bool deleted = false;

	if(!context) return MOSQ_ERR_INVAL;

	tail = context->msgs;
	while(tail){
		msg_index++;
		if(tail->state == mosq_ms_queued && msg_index <= max_inflight){
			tail->timestamp = mosquitto_time();
			if(tail->direction == mosq_md_out){
				switch(tail->qos){
					case 0:
						db__client_msg_state_set(db, context, tail, mosq_ms_publish_qos0);
						break;
					case 1:
						db__client_msg_state_set(db, context, tail, mosq_ms_publish_qos1);
						break;
					case 2:
						db__client_msg_state_set(db, context, tail, mosq_ms_publish_qos2);
						break;
				}
			}else{
				if(tail->qos == 2){
					send__pubrec(context, tail->mid);
					db__client_msg_state_set(db, context, tail, mosq_ms_wait_for_pubrel);
				}
			}
		}

		if(tail->mid == mid && tail->direction == dir){
			qos = tail->store->qos;
			topic = tail->store->topic;
			retain = tail->retain;
			source_id = tail->store->source_id;

			/* topic==NULL should be a QoS 2 message that was
			 * denied/dropped and is being processed so the client doesn't
			 * keep resending it. That means we don't send it to other
			 * clients. */
			if(!topic || !sub__messages_queue(db, source_id, topic, qos, retain, &tail->store, true)){
				db__message_remove(db, context, &tail, last);
				deleted = true;
			}else{
				return 1;
			}
		}else{
			last = tail;
			tail = tail->next;
		}
		if(msg_index > max_inflight && deleted){
			return MOSQ_ERR_SUCCESS;
		}
	}
	if(deleted){
		return MOSQ_ERR_SUCCESS;
	}else{
		return 1;
	}
}

int db__message_write(struct mosquitto_db *db, struct mosquitto *context)
{
	int rc;
	struct mosquitto_client_msg *tail, *last = NULL;
	uint16_t mid;
	int retries;
	int retain;
	const char *topic;
	int qos;
	uint32_t payloadlen;
	const void *payload;
	int msg_count = 0;

	if(!context || context->sock == INVALID_SOCKET
			|| (context->state == mosq_cs_connected && !context->id)){
		return MOSQ_ERR_INVAL;
	}

	if(context->state != mosq_cs_connected){
		return MOSQ_ERR_SUCCESS;
	}

	tail = context->msgs;
	while(tail){
		if(tail->direction == mosq_md_in){
			msg_count++;
		}
		if(tail->state != mosq_ms_queued){
			mid = tail->mid;
			retries = tail->dup;
			retain = tail->retain;
			topic = tail->store->topic;
			qos = tail->qos;
			payloadlen = tail->store->payloadlen;
			payload = UHPA_ACCESS_PAYLOAD(tail->store);

			switch(tail->state){
				case mosq_ms_publish_qos0:
					rc = send__publish(context, mid, topic, payloadlen, payload, qos, retain, retries);
					if(!rc){
						db__message_remove(db, context, &tail, last);
					}else{
						return rc;
					}
					break;

				case mosq_ms_publish_qos1:
					rc = send__publish(context, mid, topic, payloadlen, payload, qos, retain, retries);
					if(!rc){
						tail->timestamp = mosquitto_time();
						tail->dup = 1; /* Any retry attempts are a duplicate. */
						db__client_msg_state_set(db, context, tail, mosq_ms_wait_for_puback);
					}else{
						return rc;
					}
					last = tail;
					tail = tail->next;
					break;

				case mosq_ms_publish_qos2:
					rc = send__publish(context, mid, topic, payloadlen, payload, qos, retain, retries);
					if(!rc){
						tail->timestamp = mosquitto_time();
						tail->dup = 1; /* Any retry attempts are a duplicate. */
						db__client_msg_state_set(db, context, tail, mosq_ms_wait_for_pubrec);
					}else{
						return rc;
					}
					last = tail;
					tail = tail->next;
					break;
				
				case mosq_ms_send_pubrec:
					rc = send__pubrec(context, mid);
					if(!rc){
						db__client_msg_state_set(db, context, tail, mosq_ms_wait_for_pubrel);
					}else{
						return rc;
					}
					last = tail;
					tail = tail->next;
					break;

				case mosq_ms_resend_pubrel:
					rc = send__pubrel(context, mid);
					if(!rc){
						db__client_msg_state_set(db, context, tail, mosq_ms_wait_for_pubcomp);
					}else{
						return rc;
					}
					last = tail;
					tail = tail->next;
					break;

				case mosq_ms_resend_pubcomp:
					rc = send__pubcomp(context, mid);
					if(!rc){
						db__client_msg_state_set(db, context, tail, mosq_ms_wait_for_pubrel);
					}else{
						return rc;
					}
					last = tail;
					tail = tail->next;
					break;

				default:
					last = tail;
					tail = tail->next;
					break;
			}
		}else{
			/* state == mosq_ms_queued */
			if(tail->direction == mosq_md_in && (max_inflight == 0 || msg_count < max_inflight)){
				if(tail->qos == 2){
					db__client_msg_state_set(db, context, tail, mosq_ms_send_pubrec);
				}
			}else{
				last = tail;
				tail = tail->next;
			}
		}
	}

	return MOSQ_ERR_SUCCESS;
}


void db__client_msg_state_set(struct mosquitto_db *db, struct mosquitto *context, struct mosquitto_client_msg *msg, int state)
{
	if(!msg) return;

	msg->state = state;
#ifdef WITH_PERSISTENCE
	if(msg->qos > 0 && db->config->queue_qos0_messages == true){
		/* FIXME */ persist__client_msg_update(db, context->id, msg->mid, msg->direction, msg->state, msg->dup);
	}
#endif
}


void db__limits_set(int inflight, int queued)
{
	max_inflight = inflight;
	max_queued = queued;
}

void db__vacuum(void)
{
	/* FIXME - reimplement? */
}

