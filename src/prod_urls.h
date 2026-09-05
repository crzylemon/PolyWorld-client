/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: prod_urls.h                                                                         |
|   Purpose: prod site / tcp / api urls                                                       |
\*-------------------------------------------------------------------------------------------*/

#ifndef PW_PROD_URLS_H
#define PW_PROD_URLS_H

#define PW_SITE_ORIGIN       "https://polyworld.games"
#define PW_SITE_HOST         "polyworld.games"
#define PW_TCP_HOST          "tcp.polyworld.games"
#define PW_TCP_PORT          7777
#define PW_API_BASE          PW_SITE_ORIGIN "/api"
#define PW_AUTH_API_URL      PW_API_BASE "/auth.php"
#define PW_TICKET_API_URL    PW_API_BASE "/join_ticket.php"
#define PW_AVATAR_API_URL    PW_API_BASE "/avatar.php"
#define PW_CATALOG_API_URL   PW_API_BASE "/catalog_client.php"
#define PW_GAMES_API_URL     PW_API_BASE "/games.php"
#define PW_THUMBNAIL_API_URL PW_API_BASE "/thumbnail.php"
#define PW_SAVEGAME_API_URL  PW_API_BASE "/savegame.php"
#define PW_LOADGAME_API_URL  PW_API_BASE "/loadgame.php"
#define PW_TOOLBOX_API_URL   PW_API_BASE "/toolbox.php"
#define PW_UPDATE_CHECK_URL         PW_SITE_ORIGIN "/latestclient.txt"
#define PW_STUDIO_UPDATE_CHECK_URL  PW_SITE_ORIGIN "/lateststudio.txt"

#define PW_LOCAL_API_BASE    "http://127.0.0.1/api"

#endif
