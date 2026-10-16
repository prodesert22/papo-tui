# Exemplos sintéticos do contrato

Gerados por `python -m tools.contract_fixtures`. Não contêm contas, mensagens ou cookies copiados de uma instância real. As senhas em exemplos de requisição são valores sintéticos conhecidos.

`manifest.json` relaciona 62 exemplos REST com operação, media type, status, arquivo e resultado esperado de validação. Os exemplos `valid=false` exercitam ausência de senha, lotes vazios e a contradição documentada de `reactions=null`; não representam respostas que o teste considera conformes.

Os arquivos `history_{asc,desc}_page_{1,2}.json` representam 102 mensagens com o mesmo timestamp. A primeira página tem 100 itens e a segunda 2, permitindo verificar limites e uso de `since + last_id` sem depender do relógio do backend.

`websocket_events.json` contém 18 exemplos: eventos parciais de mensagem, mudanças de canal, DMs, reações, presença, digitação, heartbeat, erro e um tipo futuro. As formas dos eventos vêm da tabela descritiva de `/ws`; a especificação não fornece schemas formais para esses payloads. Tipos futuros podem ser ignorados, e ausência de campos em eventos parciais não significa remoção de dados REST.

O validador usa OpenAPI 3.0 e YAML 1.2. `off` é uma string de configuração de notificação, não o booleano false de YAML 1.1. As fixtures são verificadas pela suíte `api-contract` do CTest.
