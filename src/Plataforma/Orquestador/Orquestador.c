/*
 * Orquestador.c
 *
 *  Created on: 20/06/2013
 *      Author: reyiyo
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <commons/string.h>
#include "Orquestador.h"
#include "../../common/list.h"
#include "../Planificador/Planificador.h"

#define PUERTO_ORQUESTADOR 5000

void* orquestador(void* plat) {
	t_plataforma* plataforma = (t_plataforma*) plat;
	t_orquestador* self = orquestador_create(PUERTO_ORQUESTADOR, plataforma);

	t_socket_client* acceptClosure(t_socket_server* server) {
		t_socket_client* client = sockets_accept(server);

		t_mensaje* mensaje = mensaje_recibir(client);

		if (mensaje == NULL ) {
			sockets_destroyClient(client);
			pthread_mutex_lock(&plataforma->logger_mutex);
			log_warning(plataforma->logger,
					"Orquestador: Error al recibir datos en el accept");
			pthread_mutex_unlock(&plataforma->logger_mutex);
			return NULL ;
		}

		int tipo_mensaje = mensaje->type;

		mensaje_destroy(mensaje);

		switch (tipo_mensaje) {
		case M_HANDSHAKE_PERSONAJE:
			responder_handshake(client, plataforma->logger,
					&plataforma->logger_mutex, "Orquestador");
			break;
		case M_HANDSHAKE_NIVEL:
			responder_handshake(client, plataforma->logger,
					&plataforma->logger_mutex, "Orquestador");
			if (!procesar_handshake_nivel(self, client, plataforma)) {
				orquestador_send_error_message("Error al procesar el handshake",
						client);
				return NULL ;
			}
			break;
		default:
			pthread_mutex_lock(&plataforma->logger_mutex);
			log_warning(plataforma->logger,
					"Orquestador: Error al recibir el handshake, tipo de mensaje no valido %d",
					tipo_mensaje);
			pthread_mutex_unlock(&plataforma->logger_mutex);
			orquestador_send_error_message("Request desconocido", client);
			return NULL ;
		}

		return client;
	}

	int recvClosure(t_socket_client* client) {

		t_mensaje* mensaje = mensaje_recibir(client);

		if (mensaje == NULL ) {
			pthread_mutex_lock(&plataforma->logger_mutex);
			log_debug(plataforma->logger,
					"Orquestador: Mensaje recibido NULL.");
			pthread_mutex_unlock(&plataforma->logger_mutex);
			verificar_nivel_desconectado(plataforma, client);
			return false;
		}

		mostrar_mensaje(mensaje, client);
		process_request(mensaje, client, plataforma);

		mensaje_destroy(mensaje);
		return true;
	}

	sockets_create_little_server(plataforma->ip, self->puerto,
			plataforma->logger, &plataforma->logger_mutex, "Orquestador",
			self->servers, self->clients, &acceptClosure, &recvClosure, NULL );

	orquestador_destroy(self);

	pthread_mutex_lock(&plataforma->logger_mutex);
	log_info(plataforma->logger, "Orquestador: Server cerrado correctamente");
	pthread_mutex_unlock(&plataforma->logger_mutex);

	return (void*) EXIT_SUCCESS;

}

t_orquestador* orquestador_create(int puerto, t_plataforma* plataforma) {
	t_orquestador* new = malloc(sizeof(t_orquestador));
	new->puerto = puerto;
	new->planificadores_count = 0;
	new->clients = list_create();
	new->servers = list_create();
	plataforma->orquestador = new;
	return new;
}

void orquestador_destroy(t_orquestador* self) {
	list_destroy_and_destroy_elements(self->clients,
			(void *) sockets_destroyClient);
	list_destroy_and_destroy_elements(self->servers,
			(void *) sockets_destroyServer);
	free(self);
}

void process_request(t_mensaje* request, t_socket_client* client,
		t_plataforma* plataforma) {
	if (request->type == M_GET_INFO_NIVEL_REQUEST) {
		return orquestador_get_info_nivel(request, client, plataforma);
	} else {
		pthread_mutex_lock(&plataforma->logger_mutex);
		log_warning(plataforma->logger,
				"Orquestador: Tipo de Request desconocido: %d", request->type);
		pthread_mutex_unlock(&plataforma->logger_mutex);
		return orquestador_send_error_message("Request desconocido", client);
	}
}

void orquestador_get_info_nivel(t_mensaje* request, t_socket_client* client,
		t_plataforma* plataforma) {
	char* nivel_pedido = string_duplicate((char*) request->payload);

	plataforma_t_nivel* el_nivel = plataforma_get_nivel(plataforma,
			nivel_pedido);

	if (el_nivel == NULL ) {
		pthread_mutex_lock(&plataforma->logger_mutex);
		log_error(plataforma->logger, "Orquestador: Nivel inválido: %s",
				nivel_pedido);
		pthread_mutex_unlock(&plataforma->logger_mutex);

		free(nivel_pedido);
		orquestador_send_error_message("Nivel inválido", client);
		return;
	}

	t_mensaje* response = mensaje_create(M_GET_INFO_NIVEL_RESPONSE);

	t_stream* response_data = get_info_nivel_response_create_serialized(
			el_nivel->connection_info, el_nivel->planificador->connection_info);

	mensaje_setdata(response, response_data->data, response_data->length);
	mensaje_send(response, client);
	mensaje_destroy(response);

	free(nivel_pedido);
	free(response_data);
}

void orquestador_send_error_message(char* error_description,
		t_socket_client* client) {

	mensaje_create_and_send(M_ERROR, strdup(error_description),
			strlen(error_description) + 1, client);

}

bool procesar_handshake_nivel(t_orquestador* self,
		t_socket_client* socket_nivel, t_plataforma* plataforma) {

	t_mensaje* mensaje = mensaje_create(M_GET_NOMBRE_NIVEL_REQUEST);
	mensaje_send(mensaje, socket_nivel);
	mensaje_destroy(mensaje);

	mensaje = mensaje_recibir(socket_nivel);

	if (mensaje == NULL ) {
		sockets_destroyClient(socket_nivel);
		pthread_mutex_lock(&plataforma->logger_mutex);
		log_warning(plataforma->logger,
				"Orquestador: Error al recibir el nombre del nivel");
		pthread_mutex_unlock(&plataforma->logger_mutex);
		return false;
	}

	if (mensaje->type != M_GET_NOMBRE_NIVEL_RESPONSE) {
		sockets_destroyClient(socket_nivel);
		mensaje_destroy(mensaje);
		pthread_mutex_lock(&plataforma->logger_mutex);
		log_error(plataforma->logger,
				"Orquestador: Tipo de respuesta inválido");
		pthread_mutex_unlock(&plataforma->logger_mutex);
		return false;
	}

	char* planificador_connection_info = string_from_format("%s:%d",
			plataforma->ip, 9000 + self->planificadores_count);

	if (plataforma_create_nivel(plataforma, mensaje->payload, socket_nivel,
			planificador_connection_info) != 0) {
		mensaje_destroy(mensaje);
		sockets_destroyClient(socket_nivel);

		pthread_mutex_lock(&plataforma->logger_mutex);
		log_error(plataforma->logger,
				"Orquestador: No se pudo crear el planificador para el nivel %s",
				mensaje->payload);
		pthread_mutex_unlock(&plataforma->logger_mutex);

		return false;
	}

	self->planificadores_count++;

	free(planificador_connection_info);
	mensaje_destroy(mensaje);
	return true;
}

void mostrar_mensaje(t_mensaje* mensaje, t_socket_client* client) {
	printf("Mensaje recibido del socket: %d\n", client->socket->desc);
	printf("TYPE: %d\n", mensaje->type);
	printf("LENGHT: %d\n", mensaje->length);
	printf("PAYLOAD: %s\n", (char*) mensaje->payload);
}

void verificar_nivel_desconectado(t_plataforma* plataforma,
		t_socket_client* client) {

	bool es_el_nivel(plataforma_t_nivel* elem) {
		return sockets_equalsClients(client, elem->socket_nivel);
	}

	plataforma_t_nivel* nivel_desconectado = list_find(plataforma->niveles,
			(void*) es_el_nivel);

	if (nivel_desconectado != NULL ) {
		pthread_mutex_lock(&plataforma->logger_mutex);
		log_info(plataforma->logger,
				"Orquestador: El nivel %s se ha desconectado",
				nivel_desconectado->nombre);
		pthread_mutex_unlock(&plataforma->logger_mutex);

		planificador_destroy(nivel_desconectado->planificador);

		pthread_cancel(nivel_desconectado->thread_planificador);

		my_list_remove_and_destroy_by_condition(plataforma->niveles,
				(void*) es_el_nivel, (void*) plataforma_nivel_destroy);

		pthread_mutex_lock(&plataforma->logger_mutex);
		log_debug(plataforma->logger,
				"Orquestador: Se terminó de limpiar las estructuras");
		pthread_mutex_unlock(&plataforma->logger_mutex);
	}

}
