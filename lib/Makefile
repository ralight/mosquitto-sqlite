include ../config.mk

.PHONY : really clean install

MOSQ_OBJS=mosquitto.o \
		  handle_connack.o \
		  handle_ping.o \
		  handle_pubackcomp.o \
		  handle_publish.o \
		  handle_pubrec.o \
		  handle_pubrel.o \
		  handle_suback.o \
		  handle_unsuback.o \
		  helpers.o \
		  logging_mosq.o \
		  memory_mosq.o \
		  messages_mosq.o \
		  net_mosq.o \
		  packet_mosq.o \
		  read_handle.o \
		  send_connect.o \
		  send_disconnect.o \
		  send_mosq.o \
		  send_publish.o \
		  send_subscribe.o \
		  send_unsubscribe.o \
		  socks_mosq.o \
		  srv_mosq.o \
		  thread_mosq.o \
		  time_mosq.o \
		  tls_mosq.o \
		  util_mosq.o \
		  will_mosq.o

all : libmosquitto.so.${SOVERSION} libmosquitto.a
	$(MAKE) -C cpp

install : all
	$(INSTALL) -d ${DESTDIR}$(prefix)/lib${LIB_SUFFIX}/
	$(INSTALL) -s --strip-program=${CROSS_COMPILE}${STRIP} libmosquitto.so.${SOVERSION} ${DESTDIR}${prefix}/lib${LIB_SUFFIX}/libmosquitto.so.${SOVERSION}
	ln -sf libmosquitto.so.${SOVERSION} ${DESTDIR}${prefix}/lib${LIB_SUFFIX}/libmosquitto.so
	$(INSTALL) -d ${DESTDIR}${prefix}/include/
	$(INSTALL) mosquitto.h ${DESTDIR}${prefix}/include/mosquitto.h
	$(MAKE) -C cpp install

uninstall :
	-rm -f ${DESTDIR}${prefix}/lib${LIB_SUFFIX}/libmosquitto.so.${SOVERSION}
	-rm -f ${DESTDIR}${prefix}/lib${LIB_SUFFIX}/libmosquitto.so
	-rm -f ${DESTDIR}${prefix}/include/mosquitto.h

reallyclean : clean

clean :
	-rm -f *.o libmosquitto.so.${SOVERSION} libmosquitto.so libmosquitto.a
	$(MAKE) -C cpp clean

libmosquitto.so.${SOVERSION} : ${MOSQ_OBJS}
	${CROSS_COMPILE}$(CC) -shared $(LIB_LDFLAGS) $^ -o $@ ${LIB_LIBS}

libmosquitto.a : ${MOSQ_OBJS}
	${CROSS_COMPILE}$(AR) cr $@ $^

mosquitto.o : mosquitto.c mosquitto.h
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

handle_connack.o : handle_connack.c read_handle.h
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

handle_publish.o : handle_publish.c read_handle.h
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

handle_ping.o : handle_ping.c read_handle.h
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

handle_pubackcomp.o : handle_pubackcomp.c read_handle.h
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

handle_pubrec.o : handle_pubrec.c read_handle.h
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

handle_pubrel.o : handle_pubrel.c read_handle.h
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

handle_suback.o : handle_suback.c read_handle.h
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

handle_unsuback.o : handle_unsuback.c read_handle.h
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

helpers.o : helpers.c
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

logging_mosq.o : logging_mosq.c logging_mosq.h
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

messages_mosq.o : messages_mosq.c messages_mosq.h
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

memory_mosq.o : memory_mosq.c memory_mosq.h
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

net_mosq.o : net_mosq.c net_mosq.h
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

packet_mosq.o : packet_mosq.c packet_mosq.h
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

read_handle.o : read_handle.c read_handle.h
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

send_connect.o : send_connect.c send_mosq.h
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

send_disconnect.o : send_disconnect.c send_mosq.h
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

send_mosq.o : send_mosq.c send_mosq.h
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

send_publish.o : send_publish.c send_mosq.h
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

send_subscribe.o : send_subscribe.c send_mosq.h
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

send_unsubscribe.o : send_unsubscribe.c send_mosq.h
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

socks_mosq.o : socks_mosq.c
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

srv_mosq.o : srv_mosq.c
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

thread_mosq.o : thread_mosq.c
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

time_mosq.o : time_mosq.c
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

tls_mosq.o : tls_mosq.c
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

util_mosq.o : util_mosq.c util_mosq.h
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

will_mosq.o : will_mosq.c will_mosq.h
	${CROSS_COMPILE}$(CC) $(LIB_CFLAGS) -c $< -o $@

