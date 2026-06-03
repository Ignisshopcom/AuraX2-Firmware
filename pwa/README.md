# AuraX Finder PWA

Static PWA for `ignisshop.com/aurax`.

Deploy the contents of this `pwa` folder to `https://ignisshop.com/aurax/`.
The app is installable, caches itself for offline use, and stores manually added devices in `localStorage`.

Browser limitation: a public HTTPS PWA cannot use UDP discovery and may be blocked from reading `http://192.168.x.x/status` by mixed-content, CORS, or private-network rules. The app still opens local AuraX device pages by navigating to their `http://` address, and the firmware exposes CORS/private-network headers to support browsers that allow local probing.
