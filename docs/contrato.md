# Contrato inicial do cliente

Referência: [openapi.yml](../openapi.yml). Este documento distingue o protocolo, o fluxo implementado e as confirmações que dependem do backend real.

## Quatro campos de login

| Campo | Utilização |
| --- | --- |
| URL do servidor | Base dos endpoints, preservando prefixo de caminho |
| Usuário | `username` no login |
| Senha | `password` no login; oculta no terminal |
| Senha do servidor | `server_password` para instâncias privadas; oculta no terminal |

1. Validar URL, usuário e senha; iniciar contexto de cookies vazio.
2. `GET /server`: exigir JSON com `public` booleano; consultar `name` para apresentação.
3. Se privado, exigir senha do servidor e fazer `POST /auth/login_server` com `{"server_password":"…"}`. Esperar 204; a autorização temporária vem no cookie `Auth`.
4. `POST /auth/login` com `{"username":"…","password":"…"}`. Esperar 200; o token vem no cookie, não no JSON. Rejeitar `connection_violation=true`.
5. `GET /auth/whoami`: exigir ID e username não vazios, confirmando que o cookie autentica as requisições posteriores. Rejeitar violação de sessão.
6. `POST /auth/logout`: esperar 204 e limpar cookies locais. Em erro, também descartar a sessão local; revogação remota não foi confirmada.

Requisições serializadas não bloqueiam a UI. Senhas não são registradas. O cliente apresenta mensagens locais por fase/status, sem reproduzir corpos arbitrários de erro que podem conter segredos ou controles de terminal. Redirecionamentos exigem informar a URL final.

## Inventário para os próximos marcos

| Área | Operações documentadas | Particularidade |
| --- | --- | --- |
| Disponibilidade | `GET /health`, `GET /server` | Públicas; `/server` identifica acesso privado |
| Cadastro/sessão | Register, login_server, login, whoami, refresh, logout | Cookie HttpOnly `Auth`; refresh rotaciona sessão |
| Canais | `GET /channels` | text/category/voice, posição e tópico |
| Histórico | `GET /channels/{channel_id}/messages` | Até 100; since + last_id, asc/desc e has_more |
| Envio | `POST /messages` | Multipart inclusive para texto, com channel_id/content |
| Alterações | `PUT/DELETE /messages/{message_id}` | Atualizações também chegam via WS |
| Usuários | `GET /users`, resumos/perfis em lote | Cursor composto; limites por lote |
| DMs | `GET/POST /dms`, `GET/DELETE /dms/{dm_id}` | ID também é channel_id; fechamento preserva histórico |
| Notificações | Listagem e `PUT /users/{user_id}/read_notification` | Até 100 por página; marcação de 1 a 1000 IDs |
| Eventos | `GET /ws` com Upgrade | Mesmo cookie; mensagens, canais, DMs, presença e heartbeat |

Erros documentados seguem RFC 7807; respostas incluem `X-Request-ID`. Diagnóstico com esse ID e tratamento completo dos erros serão ampliados nos próximos marcos.

## Cobertura e pendências

`tests/mock_server.py` contém exemplos sintéticos de servidor público/privado, login, autorização temporária, whoami, logout e erros. `tests/test_auth.py` exercita transporte/URLs/cookies; `tests/test_terminal.py` exercita o formulário em pseudoterminal. Cookies sintéticos não são JWTs reais; cenários HTTP locais não comprovam TLS nem comportamento de produção.

Falta confirmar URL/acesso à instância real, contas/canais/histórico de R0, cookie Secure no desenvolvimento local, intervalos de heartbeat, efeitos de refresh/revogação WS, cursores com timestamps iguais e limites efetivos de texto/anexos.

Não há operação documentada para avançar `last_read_message`. Settings de canal atualizam preferências de notificação. O MVP mantém indicadores locais e trata notificações persistidas, sem inventar endpoint de leitura, replay WS ou federação.
