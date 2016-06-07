#undef UNICODE

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>

// Link avec ws2_32.lib
#pragma comment(lib, "ws2_32.lib")

void displayCandidates();
int  initialize();
void terminate();

SOCKET leSocket;// = INVALID_SOCKET;


int __cdecl main(int argc, char **argv)
{
	if (initialize() == 0)
	{
		printf("Bienvenue au systeme electoral automatise PolyVote\n\n");
		printf("Liste des candidats :\n");
		displayCandidates();

		printf("Veuillez entrer le numero du candidat pour lequel vous voulez voter : ");
		
		std::array < char, 64 > voteBuffer;
		gets_s(voteBuffer.data(), voteBuffer.size());

		send(leSocket, voteBuffer.data(), voteBuffer.size(), 0);

		printf("Confirmation de votre vote...");

		// On effectue 20 essais pour éviter de bloquer ici...
		bool success = false;
		for (auto i = 0; i < 20; ++i)
		{
			std::array < char, 1 > confirmBuffer;
			
			auto bytesRecv = recv(leSocket, confirmBuffer.data(), confirmBuffer.size(), 0);

			if (bytesRecv > 0 && confirmBuffer.at(0) == '1')
			{
				success = true;
				printf("SUCCES!\n\nMerci.");
				break;
			}
			else
			{
				std::cout << ".";
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
		}
		if (!success) {
			printf("Erreur!\n\nContactez un polytechnicien. Merci.");
		}
	}
	
	terminate();
	getchar();

    return 0;
}

//// initialize ///////////////////////////////////////////////////////
// Initializes the program
int initialize() {
	WSADATA wsaData;
	
	struct addrinfo *result = NULL,
		*ptr = NULL,
		hints;
	int iResult;

	//--------------------------------------------
	// InitialisATION de Winsock
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		printf("Erreur de WSAStartup: %d\n", iResult);
		return 1;
	}

	// On va creer le socket pour communiquer avec le serveur
	leSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (leSocket == INVALID_SOCKET) {
		printf("Erreur de socket(): %ld\n\n", WSAGetLastError());
		freeaddrinfo(result);
		WSACleanup();
		printf("Appuyez une touche pour finir\n");
		getchar();
		return 1;
	}

	//--------------------------------------------
	// On va chercher l'adresse du serveur en utilisant la fonction getaddrinfo.
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;        // Famille d'adresses
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;  // Protocole utilisé par le serveur

	// On indique le nom et le port du serveur auquel on veut se connecter
	char host[256];
	char port[256];

	printf("Entrez l'adresse IP du serveur : ");
	gets_s(host);

	printf("Entrez le port : ");
	gets_s(port);

	// getaddrinfo obtient l'adresse IP du host donné
	iResult = getaddrinfo(host, port, &hints, &result);
	if (iResult != 0) {
		printf("Erreur de getaddrinfo: %d\n", iResult);
		WSACleanup();
		return 1;
	}

	//---------------------------------------------------------------------		
	//On parcours les adresses retournees jusqu'a trouver la premiere adresse IPV4
	while ((result != NULL) && (result->ai_family != AF_INET))
		result = result->ai_next;

	//-----------------------------------------
	if (((result == NULL) || (result->ai_family != AF_INET))) {
		freeaddrinfo(result);
		printf("Impossible de recuperer la bonne adresse\n\n");
		WSACleanup();
		printf("Appuyez une touche pour finir\n");
		getchar();
		return 1;
	}

	sockaddr_in *adresse;
	adresse = (struct sockaddr_in *) result->ai_addr;

	//----------------------------------------------------
	printf("Adresse trouvee pour le serveur %s : %s\n\n", host, inet_ntoa(adresse->sin_addr));
	printf("Tentative de connexion au serveur %s avec le port %s\n\n", inet_ntoa(adresse->sin_addr), port);

	// On va se connecter au serveur en utilisant l'adresse qui se trouve dans
	// la variable result.
	iResult = connect(leSocket, result->ai_addr, (int)(result->ai_addrlen));
	if (iResult == SOCKET_ERROR) {
		printf("Impossible de se connecter au serveur %s sur le port %s\n\n", inet_ntoa(adresse->sin_addr), port);
		freeaddrinfo(result);
		WSACleanup();
		printf("Appuyez une touche pour finir\n");
		getchar();
		return 1;
	}

	printf("Connecte au serveur %s:%s\n\n", host, port);
	freeaddrinfo(result);

	return 0;
}

//// displayCandidates ///////////////////////////////////////////////////////
// Prints to user the list of candidates received from the server
void displayCandidates() {
	std::array<char, 1024> reply;
	reply.fill('\0');

	auto charToRecv = 0;

	recv(leSocket, reply.data(), 1024, 0);
	for (auto c : reply) {
		if (c == '\0') {
			break;
		}

		std::cout << c;
	}
}

//// terminate ///////////////////////////////////////////////////////
// Cleans the sockets
void terminate() {
	closesocket(leSocket);
	WSACleanup();
}
