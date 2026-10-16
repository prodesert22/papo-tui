# Instruções para agentes

Este arquivo define como trabalhar neste repositório. Leia-o antes de alterar arquivos.

## Projeto

Papo TUI é um cliente de conversas para terminal em C. O contrato do serviço está em `openapi.yml`; `README.md` e `docs/uso.md` descrevem instalação, execução e uso.

O cliente usa C17, CMake, ncursesw/formw, libcurl, Jansson e bibliotecas de mídia. Há testes HTTP com backend simulado e de interação em pseudoterminal. Não tratar a tela inicial como conclusão de toda a integração. Consulte `src/net/auth/` para os fluxos implementados e `CONTRIBUTING.md` para desenvolvimento e distribuição.

`tools/` contém inventário, geração/validação de fixtures e probe da instância de desenvolvimento. `tls_proxy.go` usa a biblioteca padrão Go para verificações HTTPS/WSS. As ferramentas são de desenvolvimento. Diferencie observação real de fixtures. Inventários e relatórios de execução ficam em `build/`; dados locais de contas de teste e cookies não devem entrar em commits. O modo `--username` lê a senha do ambiente sem persistir credenciais.

## Regras de trabalho

1. Leia `README.md`, `docs/uso.md` e os trechos relevantes de `openapi.yml` antes de propor ou implementar uma entrega.
2. Mantenha cada mudança limitada à entrega em andamento. Não misture funcionalidades futuras ou refatorações sem relação com a tarefa.
3. Não invente endpoints, campos, eventos, permissões ou garantias de sincronização. Quando a especificação não definir um comportamento, registre a lacuna e escolha uma solução que não prometa suporte inexistente no backend.
4. Trate cada instância como um contexto isolado. Cookies, estado de sessão e entidades do mesmo UUID em instâncias diferentes não podem ser compartilhados.
5. Nunca grave senhas ou cookies em logs, arquivos de configuração versionados ou mensagens de erro. Redija segredos de saídas de diagnóstico.
6. Considere todo conteúdo remoto como texto não confiável. Não permita que mensagens, nomes ou tópicos emitam sequências de controle do terminal.
7. Em C, defina claramente quem possui e libera cada buffer e objeto. Verifique limites, falhas de alocação, cancelamento e encerramento de conexões.
8. Mantenha `README.md` e `docs/` coerentes com o comportamento realmente implementado. Reserve `docs/` exclusivamente à documentação de uso do programa, como instalação, execução e controles. Não adicione inventários de API, planejamento, decisões internas, fixtures, relatórios de testes ou diagnósticos do backend nesse diretório. Alterações no contrato OpenAPI exigem confirmação do comportamento do backend.
9. Prefira alterações pequenas e revisáveis. Não declare uma entrega concluída sem cumprir os critérios de aceite e registrar evidências. Registre novas funcionalidades e suas dependências antes de alterar o escopo do MVP.
10. Use bibliotecas existentes para a funcionalidade que será adicionada. Antes de escrever uma implementação própria, verifique se as dependências do projeto já oferecem o recurso; caso não ofereçam, pesquise bibliotecas compatíveis com C e com a stack do projeto. Avalie manutenção, licença, documentação e custo de integração. Implemente o recurso do zero somente quando não houver biblioteca adequada e registre a justificativa técnica.
11. Escreva todas as mensagens de commit em inglês, incluindo o título e o corpo quando houver.
12. Todo novo envio HTTP do cliente deve passar pelo orçamento global de `src/net/rate_limit.c`: padrão de 10 requisições/s e burst de 25. A espera deve ocorrer no ciclo de eventos, sem bloquear a TUI. Não reinicie o orçamento ao autenticar, cancelar, sair da conta ou recriar um cliente; não repita mutações automaticamente ao receber 429. As ferramentas de integração devem manter cadência compatível com esses limites.
13. A integração confirmou que GET de histórico marca leitura remota. Evite prefetch de históricos de conversas inativas. O WS pode responder heartbeat após expiração/revogação individual; revalide a sessão por REST e não trate ACK como autenticação. Mantenha verificação TLS ativa; `CURL_CA_BUNDLE` permite confiar em uma CA própria.
14. Escreva `docs/uso.md` em inglês e organize-o como um guia de usuário: como executar o programa, como usar seus recursos e como resolver problemas comuns. Preserve os nomes reais de botões e avisos da interface quando necessário, acompanhados de explicação em inglês. Mantenha detalhes de implementação, arquitetura e desenvolvimento em `CONTRIBUTING.md`; relatórios e diagnósticos de execução ficam em `build/`.

## Build e validação

O build utiliza CMake e pkg-config. Mantenha os pré-requisitos e comandos no README alinhados ao código. Não afirme que uma validação foi executada sem evidência. Os comandos são:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Para testes Python, instale `requirements-dev.txt` em um ambiente virtual e indique seu interpretador ao CMake, conforme README. Use YAML 1.2 e validação OpenAPI 3.0: não transformar enum `off` em booleano nem ignorar `nullable`. O inventário e as fixtures são gerados por `tools.contract_inventory` e `tools.contract_fixtures`; atualize-os com seus geradores.

Execute os testes apropriados às mudanças antes de afirmar que estão validadas. Para mudanças na integração com o backend, inclua cenários de falha e valide contra a instância de desenvolvimento quando disponível. Não coloque credenciais reais em fixtures.

## Organização pretendida

`src/app/` contém ciclo e estado do aplicativo; `src/ui/` separa telas, componentes, entrada e mídia; `src/net/` separa autenticação/API, HTTP e tempo real. Modelos e configuração ficam em `src/model/` e `src/config/`; `src/platform/` adapta recursos nativos de Linux e Windows. `tests/` separa testes unitários, integração, terminal, contrato, distribuição e suporte; `docs/` documenta instalação e uso. Só criar módulos e diretórios quando houver código que os utilize.
