function IgnisPreview(ignis) {
    this.ignis = ignis;
    ignis.preview = this;

    this.initialized = false;
    this.stats = null;
    this.renderer = null;
    this.camera = null;
    this.texture = null;
    this.mesh = null;
    this.meshes = [];
    this.material = null;
    this.materials = [];
    this.running = [];
    this.lt = 0;
    this.lts = [];
    this.ledCounts = [];
    this.uniforms_list = [];
    this.instances = config.preview_instances;
    this.currentTextures = [];
    this.nodes = [];
    this.loadings = [];
    this.tex_timeout = {};
    this.ratios = [];
    this.mdown = false;
    this.rdown = false;
    this.mx = this.my = 0;
    this.ox = this.oy = 0;
    this.cx = this.cy = 0;
    this.cz = 3;
    this.labels = [];
    this.layout = 0;
    this.preview_indexes = [];
    this.preview_indexes_shown = [];
    this.multi_preview = false;
    this.multi_meshes = {};
    this.multi_materials = {};
    this.multi_uniforms = {};
    this.multi_nodes = {};
    this.multi_nodes_current = {};
    this.multi_ratios = {};
    this.multi_labels = {};
    this.multi_textures = {};
    this.timelineAnchors = [];
    this.multiTimelineAnchors = {};

    for (var i = 0; i < this.instances; i++) {
        this.running.push(false);
        this.nodes.push(null);
        this.ledCounts.push(0);
        this.ratios.push(1);
    }

    app_register_event('project_leds_updated', $.proxy(this.updateLeds, this));

    this.currentTexture = null;
}

IgnisPreview.prototype.init = function () {
    console.log("Initializing IgnisPreview...");

    if (this.initialized) {
        console.warn("IgnisPreview has already been initialized.");
        return;
    }

    // renderer
    try {
        this.renderer = new THREE.WebGLRenderer();
        console.log("WebGLRenderer initialized");
    } catch (err) {
        console.error("WebGLRenderer initialization failed: ", err);
        return false;
    }
    this.renderer.setSize($('#preview-canvas').width(), $('#preview-canvas').height());
    $('#preview-canvas').append(this.renderer.domElement);

    // stats
    this.stats = new Stats();
    //$('#preview-canvas').append(this.stats.dom);

    // scene
    this.scene = new THREE.Scene();

    // camera
    this.camera = new THREE.PerspectiveCamera(60, this.aspectRatio(), 0.1, 10);
    this.scene.add(this.camera);
    this.camera.position.set(1.5, 0, 3);

    // uniforms & materials
    for (var i = 0; i < this.instances; i++) {
        this.uniforms_list.push(this.createUniforms());
        this.currentTextures.push(null);
        this.lts[i] = 0;
        this.loadings[i] = false;

        this.preview_indexes[i] = this.preview_indexes_shown[i] = 0;
        this.labels[i] = $('<div class="label preview-label" id="preview-label-' + i + '"><div class="number">0</div></div>');
        $('#preview-labels').append(this.labels[i]);
    }

    // texture
    this.setTexture("img/unnamed.jpg");

    // materials
    for (var i = 0; i < this.instances; i++) {
        this.materials.push(this.createMaterial(this.uniforms_list[i]));
        console.log("Material created for instance", i);
    }
    this.material = this.materials[0];

    // geometry & mesh
    var geometry = new THREE.PlaneGeometry(1, 1);
    for (var i = 0; i < this.instances; i++) {
        var mesh = new THREE.Mesh(geometry, this.materials[i]);
        mesh.rotation.z = -Math.PI / 2;
        mesh.position.x = i;
        this.scene.add(mesh);
        this.meshes.push(mesh);
        console.log("Mesh created for instance", i);
    }
    this.mesh = this.meshes[0];

    this.setLayout(0);

    $(window).on('resize', $.proxy(this.resize, this));

    this.resize();
    this.animate(0);

    this.initialized = true;
    console.log("IgnisPreview initialized");

    setInterval($.proxy(function () { this.recalculate(); }, this), 100);

    $('.preview-mode-button').on('click', $.proxy(this.switchMode, this));
    $('.preview-layout-button').on('click', $.proxy(this.switchLayout, this));
    $('#preview-overlay').on('mousewheel', $.proxy(this.mouseWheel, this));
    $('#preview-overlay').on('mousedown', $.proxy(this.mouseDown, this));
    $(document).on('mouseup', $.proxy(this.mouseUp, this));
    $('#preview-overlay').on('mousemove', $.proxy(this.mouseMove, this));

    setInterval($.proxy(this.updatePreviewIndexes, this), 100);

    this.updateMultiPreview();

    console.log("IgnisPreview setup complete");
}

IgnisPreview.prototype.switchMode = function (e) {
    var el = e.target;
    if (!el.hasAttribute('mode')) el = $(el).parent();
    var mode = parseFloat($(el).attr('mode'));
    $('.preview-mode-button').removeClass('selected');
    $(el).addClass('selected');

    var i = this.ignis.project.getCurrentTimeline().preview;
    this.uniforms_list[i].line_mode.value = mode;
    if (this.nodes[i]) {
        this.nodes[i].preview_type = mode;
    }
}


IgnisPreview.prototype.autoAlign = function () {
    switch (this.layout) {
        case 0: // 4 in line
            this.autoAlignInLine();
            break;
        case 1: // 4 in square
            this.autoAlignInSquare();
            break;
        case 2: // multi-preview
            this.autoAlignMulti();
            break;
    }
}

IgnisPreview.prototype.autoAlignInLine = function () {
    this.cx = this.camera.position.x = (this.ignis.project.timelinesCount() - 1) / 2;
}

IgnisPreview.prototype.autoAlignInSquare = function () {
    // nothing to do really
}

IgnisPreview.prototype.autoAlignMulti = function () {
    this.cx = this.camera.position.x = (this.ignis.project.timelinesCount() - 1) / 10;
}

IgnisPreview.prototype.updateMultiPreview = function () {
    if (!this.scene) return;

    const project = this.ignis.project;
    var geometry = new THREE.PlaneGeometry(1, .04);

    // remove old
    for (var hash in this.multi_meshes) {
        if (!project.timelines[hash]) {
            this.scene.remove(this.multi_meshes[hash]);
            delete (this.multi_meshes[hash]);
            delete (this.multi_materials[hash]);
            delete (this.multi_uniforms[hash]);
            delete (this.multi_nodes[hash]);
            delete (this.multi_nodes_current[hash]);
            delete (this.multi_ratios[hash]);
            this.multi_labels[hash].remove();
            delete (this.multi_labels[hash]);
            delete (this.multi_textures[hash]);
        }
    }

    // create or update
    var x = 0;
    for (var hash in project.timelines) {
        if (!this.multi_meshes[hash]) {
            this.multi_nodes[hash] = false;
            this.multi_nodes_current[hash] = 0;
            this.multi_ratios[hash] = 0;
            this.multi_textures[hash] = '';
            // uniforms & texture
            this.multi_uniforms[hash] = this.createUniforms();
            var texture = new THREE.TextureLoader().load('img/loading_tex.png');
            texture.minFilter = THREE.NearestFilter;
            texture.magFilter = THREE.NearestFilter;
            texture.wrapS = THREE.RepeatWrapping; //ClampToEdgeWrapping;//RepeatWrapping;
            texture.wrapT = THREE.ClampToEdgeWrapping;
            this.multi_uniforms[hash].texture = { type: "t", value: texture };

            // material
            this.multi_materials[hash] = this.createMaterial(this.multi_uniforms[hash], 'multi');

            // mesh
            var mesh = new THREE.Mesh(geometry, this.multi_materials[hash]);
            mesh.rotation.z = - Math.PI / 2;
            mesh.position.z = 0;
            //mesh.position.x = i;
            this.scene.add(mesh);
            this.multi_meshes[hash] = mesh;

            this.multi_labels[hash] = $('<div class="label multi-preview-label" id="multi-preview-label-' + hash + '"><div class="number"></div></div>');
            this.multi_labels[hash].find('.number').text(x + 1);
            $('#preview-labels').append(this.multi_labels[hash]);
        } else {
            this.multi_labels[hash].empty();
            this.multi_labels[hash].append($('<div class="number"></div>').text(x + 1));
        }
        this.multi_meshes[hash].position.x = x * 0.2;
        x++;
    }
}

IgnisPreview.prototype.updatePreviewIndexes = function () {
    const project = this.ignis.project;

    var i = 1;
    for (var hash in project.timelines) {
        var tl = project.timelines[hash];
        if (tl.preview !== false && tl.preview !== null) {
            this.preview_indexes[tl.preview] = i;
        }
        i++;
    }
}

IgnisPreview.prototype.updateLeds = function () {
    if (!this.uniforms_list || this.uniforms_list.length <= 0) return;

    for (var i = 0; i < this.instances; i++) {
        var tl = this.ignis.project.getTimelineByPreview(i);
        if (tl) {
            var ledCount = parseFloat(tl.leds);
            if (!isNaN(ledCount)) {
                this.uniforms_list[i].led_count.value = ledCount;
            }
        }

        if (this.currentTextures[i] && this.ledCounts[i] != ledCount) {
            this.ledCounts[i] = ledCount;
            var t = Math.floor((new Date()).getTime() / 1000);
            var tex = this.currentTextures[i];
            tex = tex.split('?')[0];
            tex = tex.replace(/_[0-9]+\.jpg$/, '_' + ledCount + '.jpg');
            tex = tex + '?t=' + t;
            this.currentTextures[i] = tex;
            this.resetTextureIdx(i);
        }
    }
}

IgnisPreview.prototype.nodeChanged = function () {
    this.recalculate();
}

IgnisPreview.prototype.setDim = function (dim) {
    for (var i = 0; i < this.instances; i++) {
        this.setDimIdx(i, dim);
    }
}

IgnisPreview.prototype.setDimIdx = function (i, dim) {
    if (i === false || i === null || i === undefined) return;
    this.uniforms_list[i].dim.value = dim;
}

IgnisPreview.prototype.resetTexture = function () {
    for (var i = 0; i < this.instances; i++) {
        this.resetTextureIdx(i);
    }
}

IgnisPreview.prototype.resetTextureIdx = function (i) {
    var t = Math.floor((new Date()).getTime() / 1000);
    var tex = this.currentTextures[i];
    if (tex) tex = tex.split('?')[0] + '?t=' + t;

    if (tex && tex.length > 0 && !fs.existsSync(tex.split('?')[0])) {
        setTimeout($.proxy(function () {
            this.resetTextureIdx(i);
        }, this), 500);
        return;
    }

    this.currentTextures[i] = null;
    this.setTextureIdx(i, tex, this.nodes[i], this.ratios[i]);
}

IgnisPreview.prototype.setTexture = function (path, node, ratio) {
    for (var i = 0; i < this.instances; i++) {
        this.setTextureIdx(i, path, node, ratio);
    }
}

IgnisPreview.prototype.setTextureIdx = function (i, path, node, ratio) {
    this.ratios[i] = (ratio ? ratio : 1);
    if (node) {
        if (this.tex_timeout[node.hash]) {
            clearTimeout(this.tex_timeout[node.hash]);
        }

        if (!node.dim) node.dim = 1;
        this.nodes[i] = node;
        this.uniforms_list[i].dim.value = parseFloat(node.dim) / 100;
        this.uniforms_list[i].line_mode.value = node.preview_type;

        if (this.ignis.project.getCurrentTimeline().preview == i) {
            $('.preview-mode-button').removeClass('selected');
            $('.preview-mode-button[mode=' + node.preview_type + ']').addClass('selected');
        }
    } else {
        this.nodes[i] = null;
        return;
    }

    this.recalculateIdx(i);

    this.uniforms_list[i].time_shift.value = this.uniforms_list[i].time.value;

    if (this.currentTextures[i] == path) return;

    var fex = fs.existsSync(path.split('?')[0]);
    var stats = false;
    if (fex) {
        stats = fs.statSync(path.split('?')[0]);
    }

    if (!fex || stats.size <= 0) {
        this.uniforms_list[i].time_shift.value = 0.0;
        this.tex_timeout[node.hash] = setTimeout($.proxy(function () {
            this.setTextureIdx(i, path, node, ratio);
        }, this), 100);

        if (!this.loadings[i]) {
            this.loadings[i] = true;
            var texture = new THREE.TextureLoader().load('img/loading_tex.png');
            texture.minFilter = THREE.NearestFilter;
            texture.magFilter = THREE.NearestFilter;
            texture.wrapS = THREE.RepeatWrapping; //ClampToEdgeWrapping;//RepeatWrapping;
            texture.wrapT = THREE.ClampToEdgeWrapping;

            this.uniforms_list[i].texture = { type: "t", value: texture };
        }

        return;
    } else {
        this.loadings[i] = false;
        this.currentTextures[i] = path;
    }

    var texture = new THREE.TextureLoader().load(path);
    var smoothEffect = node && (node.type == 'effect' || node.effectId !== undefined || node.isEffect);
    texture.minFilter = smoothEffect ? THREE.LinearFilter : THREE.NearestFilter;
    texture.magFilter = smoothEffect ? THREE.LinearFilter : THREE.NearestFilter;
    texture.wrapS = THREE.RepeatWrapping; //ClampToEdgeWrapping;//RepeatWrapping;
    texture.wrapT = THREE.ClampToEdgeWrapping;

    this.uniforms_list[i].texture = { type: "t", value: texture };
}

IgnisPreview.prototype.syncTimelinePositionIdx = function (i, node, cursorMs, renderTimestamp, locked)
{
    if (!node || !this.uniforms_list[i]) return;

    var localMs = cursorMs - node.start;
    if (!isFinite(localMs) || localMs < 0) localMs = 0;

    var renderSeconds = renderTimestamp / 1000;
    if (!isFinite(renderSeconds)) renderSeconds = this.uniforms_list[i].time.value || 0;

    if (locked) {
        this.timelineAnchors[i] = null;
        this.uniforms_list[i].time_shift.value = renderSeconds - (localMs / 1000);
        return;
    }

    var anchor = this.timelineAnchors[i];
    if (!anchor || anchor.uid != node.uid || Math.abs(anchor.cursorMs - cursorMs) > 50) {
        anchor = this.timelineAnchors[i] = {
            uid: node.uid,
            cursorMs: cursorMs,
            renderSeconds: renderSeconds,
            localSeconds: localMs / 1000
        };
    }

    this.uniforms_list[i].time_shift.value = anchor.renderSeconds - anchor.localSeconds;
}

IgnisPreview.prototype.syncMultiTimelinePosition = function (hash, node, cursorMs, renderTimestamp, locked)
{
    if (!node || !this.multi_uniforms[hash]) return;

    var localMs = cursorMs - node.start;
    if (!isFinite(localMs) || localMs < 0) localMs = 0;

    var renderSeconds = renderTimestamp / 1000;
    if (!isFinite(renderSeconds)) renderSeconds = this.multi_uniforms[hash].time.value || 0;

    if (locked) {
        this.multiTimelineAnchors[hash] = null;
        this.multi_uniforms[hash].time_shift.value = renderSeconds - (localMs / 1000);
        return;
    }

    var anchor = this.multiTimelineAnchors[hash];
    if (!anchor || anchor.uid != node.uid || Math.abs(anchor.cursorMs - cursorMs) > 50) {
        anchor = this.multiTimelineAnchors[hash] = {
            uid: node.uid,
            cursorMs: cursorMs,
            renderSeconds: renderSeconds,
            localSeconds: localMs / 1000
        };
    }

    this.multi_uniforms[hash].time_shift.value = anchor.renderSeconds - anchor.localSeconds;
}

IgnisPreview.prototype.getEffectiveTextureWidth = function (ledsHeight, ratio, node)
{
    ledsHeight = parseInt(ledsHeight);
    if (isNaN(ledsHeight) || ledsHeight <= 0) ledsHeight = config.project.default_leds;
    ratio = parseFloat(ratio);
    if (!isFinite(ratio) || ratio <= 0) ratio = 1;

    var effectiveHeight = ledsHeight;

    if (node && node.mirror) {
        var mirrorGap = parseInt(node.mgap);
        if (isNaN(mirrorGap)) mirrorGap = 0;
        if (mirrorGap % 2 != 0) mirrorGap += 1;
        effectiveHeight = Math.max(1, Math.floor((ledsHeight - mirrorGap) / 2));
    }

    return Math.max(1, Math.round(effectiveHeight * ratio));
}

IgnisPreview.prototype.recalculateIdx = function (i) {
    var tl = this.ignis.project.getTimelineByPreview(i);
    if (tl) {
        var leds_height = parseInt(tl.leds);

        if (!this.nodes[i]) return;

        var leds_width = this.getEffectiveTextureWidth(leds_height, this.ratios[i], this.nodes[i]);

        this.uniforms_list[i].stick_speed.value = 1.354;
        var led_speed = (this.nodes[i].frequency / (this.uniforms_list[i].line_mode.value == 0 ? this.uniforms_list[i].stick_speed.value : 1.0)) / (leds_width + this.nodes[i].gap);

        if (this.nodes[i].accelerometer) {
            led_speed = this.nodes[i].picture_frequency;
        }

        this.uniforms_list[i].led_speed.value = led_speed;

        this.uniforms_list[i].gap.value = 1 + this.nodes[i].gap / leds_width;
    }
}

IgnisPreview.prototype.recalculateMultiPreview = function () {
    if (!this.multi_preview) return;

    const project = this.ignis.project;
    for (var hash in project.timelines) {
        var tl = project.timelines[hash];
        if (tl) {
            var leds_height = parseInt(tl.leds);

            var node = this.multi_nodes[hash];
            if (!node) continue;

            var leds_width = this.getEffectiveTextureWidth(leds_height, this.multi_ratios[hash], node);

            this.multi_uniforms[hash].stick_speed.value = 1.354;
            var led_speed = (node.frequency / (1.0)) / (leds_width + node.gap);

            if (node.accelerometer) {
                led_speed = node.picture_frequency;
            }

            this.multi_uniforms[hash].led_speed.value = led_speed;
            this.multi_uniforms[hash].dim.value = parseFloat(node.dim) / 100;

            this.multi_uniforms[hash].gap.value = 1 + node.gap / leds_width;
        }
    }
}

IgnisPreview.prototype.recalculate = function () {
    for (var i = 0; i < this.instances; i++) {
        this.recalculateIdx(i);
    }

    this.recalculateMultiPreview();
}

IgnisPreview.prototype.createUniforms = function () {
    var uniforms = {
        time: { value: 0.0 },
        time_scale: { value: 2.0 },
        dt: { value: 0.0 },
        stick_speed: { value: 1.0 },
        stick_ratio: { value: 0.25 },
        led_speed: { value: 1.0 },
        led_count: { value: 80.0 },
        led_lines: { value: 0.0 },
        dim: { value: 1.0 },
        gap: { value: 1.0 },
        line_mode: { value: 0.0 },
        time_shift: { value: 0.0 },
        texture: null
    };
    return uniforms;
}

IgnisPreview.prototype.createMaterial = function (unifiorms, shader) {
    if (!shader) shader = 'preview';
    var basepath = window.electronApi.getAppPath();
    var vertex = fs.readFileSync(basepath + '/src/shaders/preview.vertex', 'utf-8');
    var fragment = fs.readFileSync(basepath + '/src/shaders/' + shader + '.fragment', 'utf-8');

    var material = new THREE.ShaderMaterial({
        uniforms: unifiorms,
        vertexShader: vertex, //document.getElementById('vertexShader').textContent,
        fragmentShader: fragment, //document.getElementById('fragmentShader').textContent,
        precision: 'highp',
        side: THREE.DoubleSide,
        vertexColors: THREE.NoColors,
    });
    return material;
}

IgnisPreview.prototype.projectToScreen = function (vector) {
    var widthHalf = 0.5 * this.renderer.context.canvas.width;
    var heightHalf = 0.5 * this.renderer.context.canvas.height;

    vector.project(this.camera);

    return {
        x: (vector.x * widthHalf) + widthHalf,
        y: - (vector.y * heightHalf) + heightHalf,
    };
};

IgnisPreview.prototype.setLayout = function (l) {
    this.multi_preview = false;
    $('#preview-mode-buttons').show();

    this.layout = l;
    if (l == 0) {
        for (var i = 0; i < this.instances; i++) {
            this.meshes[i].position.x = i;
            this.meshes[i].position.y = 0;
        }
        this.autoAlign();
        return;
    }

    if (l == 1) {
        this.cx = 0;
        for (var i = 0; i < this.instances; i++) {
            var s = Math.sqrt(this.instances);
            var y = Math.floor(i / s);
            var x = (i - (y * s));
            this.meshes[i].position.x = x - 0.5;
            this.meshes[i].position.y = 0.5 - y;
            this.camera.position.x = 0;
        }
        this.autoAlign();
        return;
    }

    if (l == 2) {
        this.multi_preview = true;
        $('#preview-mode-buttons').hide();
        this.autoAlign();
        return;
    }
}

IgnisPreview.prototype.mouseDown = function (e) {
    this.mdown = (e.originalEvent.button == 0 && (this.layout == 0 || this.layout == 2));
    this.rdown = (e.originalEvent.button == 2);
    this.mx = e.originalEvent.offsetX;
    this.my = e.originalEvent.offsetY;
    this.ox = this.cx;
    this.oy = this.cy;
}

IgnisPreview.prototype.mouseUp = function (e) {
    this.mdown = this.rdown = false;
    this.updateCamera();
}

IgnisPreview.prototype.mouseMove = function (e) {
    if (this.mdown) {
        var x = this.mx - e.originalEvent.offsetX;
        var y = this.my - e.originalEvent.offsetY;

        this.cx = this.ox + x / 100;

        this.updateCamera();
    }
}

IgnisPreview.prototype.updateCamera = function () {
    if (this.cx > 3) this.cx = 3;
    if (this.cx < 0) this.cx = 0;
    if (this.cz > 10) this.cz = 10;
    if (this.cz < 1) this.cz = 1;

    this.camera.position.x = this.cx;
    this.camera.position.z = this.cz;
}

IgnisPreview.prototype.mouseWheel = function (e) {
    this.cz -= e.originalEvent.wheelDelta / 300;
    this.updateCamera();
}

IgnisPreview.prototype.switchMode = function (e) {
    var el = e.target;
    if (!el.hasAttribute('mode')) el = $(el).parent();
    var mode = parseFloat($(el).attr('mode'));
    $('.preview-mode-button').removeClass('selected');
    $(el).addClass('selected');

    var i = this.ignis.project.getCurrentTimeline().preview;
    this.uniforms_list[i].line_mode.value = mode;
    if (this.nodes[i]) {
        this.nodes[i].preview_type = mode;
    }
}

IgnisPreview.prototype.switchLayout = function (e) {
    var el = e.target;
    if (!el.hasAttribute('layout')) el = $(el).parent();
    var layout = parseInt($(el).attr('layout'));
    this.setLayout(layout);
    $('.preview-layout-button').removeClass('selected');
    $('.preview-layout-button[layout=' + layout + ']').addClass('selected');
}

IgnisPreview.prototype.getMultiTextureFile = function (hash, current_node) {
    var tl = this.ignis.project.timelines[hash];
    if (current_node && current_node.type == 'effect') {
        return this.ignis.project.ensureEffectTexture(current_node, tl.leds).path;
    }
    var file_hash = current_node.hash + (current_node.mirror ? (current_node.rotate ? '_r' : '_m') + (current_node.reverse ? 'v' : '') + current_node.mgap : '');
    var tex_file = ignis_texdir() + '/' + file_hash + '_' + tl.leds + '.jpg';
    return tex_file;
}

IgnisPreview.prototype.multiTextureNeedUpdate = function (hash, current_node) {
    var fn = this.getMultiTextureFile(hash, current_node);
    return (fn != this.multi_textures[hash]);
}

IgnisPreview.prototype.setMultiTexture = function (hash, current_node) {
    var t = Math.floor((new Date()).getTime() / 1000);
    var tex_file = this.getMultiTextureFile(hash, current_node);

    if (fs.existsSync(tex_file)) {
        var stats = fs.statSync(tex_file);
        if (stats.size <= 0) return;
        this.multi_textures[hash] = tex_file;
    } else {
        return;
    }

    tex_file = tex_file + '?t=' + t;

    var texture = new THREE.TextureLoader().load(tex_file);
    var smoothEffect = current_node && (current_node.type == 'effect' || current_node.effectId !== undefined || current_node.isEffect);
    texture.minFilter = smoothEffect ? THREE.LinearFilter : THREE.NearestFilter;
    texture.magFilter = smoothEffect ? THREE.LinearFilter : THREE.NearestFilter;
    texture.wrapS = THREE.RepeatWrapping;
    texture.wrapT = THREE.ClampToEdgeWrapping;

    this.multi_uniforms[hash].texture = { type: "t", value: texture };

    this.multi_uniforms[hash].time_shift.value = this.multi_uniforms[hash].time.value;
}

IgnisPreview.prototype.animate = function (timestamp) {
    // multi previews
    if (this.multi_preview) {
        for (var hash in this.multi_uniforms) {
            // labels
            var vector = new THREE.Vector3();
            vector.add(this.multi_meshes[hash].position);
            vector.y = vector.y + 0.6;
            var proj = this.projectToScreen(vector);
            $('#multi-preview-label-' + hash).css('left', proj.x + 'px');
            $('#multi-preview-label-' + hash).css('top', proj.y + 'px');
            $('#multi-preview-label-' + hash).show();

            //
            if (!this.multi_nodes[hash]) {
                this.multi_meshes[hash].visible = false;
                continue;
            }
            this.multi_meshes[hash].visible = true;
            this.multi_uniforms[hash]["time"].value = (timestamp / 1000);

            if (this.multi_nodes_current[hash] != this.multi_nodes[hash].uid) {
                this.multi_nodes_current[hash] = this.multi_nodes[hash].uid;
                this.setMultiTexture(hash, this.multi_nodes[hash]);
            } else if (this.multiTextureNeedUpdate(hash, this.multi_nodes[hash])) {
                this.setMultiTexture(hash, this.multi_nodes[hash]);
            }
        }
    } else {
        for (var hash in this.multi_uniforms) {
            this.multi_meshes[hash].visible = false;
        }
        $('.multi-preview-label').hide();
    }

    // classic previews
    if (!this.multi_preview) {
        if (this.uniforms_list.length > 0) {
            for (var i = 0; i < this.instances; i++) {

                if (this.nodes[i]) {
                    this.uniforms_list[i].time_scale.value = (this.nodes[i].accelerometer ? 1.0 : 2.0);
                }

                var t = (timestamp / (this.uniforms_list[i]["line_mode"].value == 0.0 ? 1000 : 1000));
                var dt = (t - this.lts[i]) / 10;
                this.lts[i] = t;

                this.uniforms_list[i]["time"].value = t;// / 1000.0;

                this.meshes[i].visible = this.running[i];
            }

            for (var i = 0; i < this.instances; i++) {
                if (this.meshes[i].visible) {
                    var vector = new THREE.Vector3();
                    vector.add(this.meshes[i].position);
                    if (this.layout == 0) {
                        vector.y = vector.y + 0.6;
                    }
                    if (this.layout == 1) {
                        vector.y = vector.y * 2.2;
                    }
                    var proj = this.projectToScreen(vector);
                    if (this.preview_indexes_shown[i] != this.preview_indexes[i]) {
                        this.preview_indexes_shown[i] = this.preview_indexes[i];
                        $('#preview-label-' + i).find('.number').text(this.preview_indexes[i]);
                    }
                    $('#preview-label-' + i).css('left', proj.x + 'px');
                    $('#preview-label-' + i).css('top', proj.y + 'px');
                    $('#preview-label-' + i).show();
                } else {
                    $('#preview-label-' + i).hide();
                }
            }
        }
    } else {
        if (this.uniforms_list.length > 0) {
            for (var i = 0; i < this.instances; i++) {
                this.meshes[i].visible = false;
            }
        }
        $('.preview-label').hide();
    }

    // render
    if (this.renderer) {
        this.renderer.render(this.scene, this.camera);
        this.stats.update();
    }
}

IgnisPreview.prototype.aspectRatio = function () {
    return ($('#preview-canvas').width() / $('#preview-canvas').height());
}

IgnisPreview.prototype.resize = function (e) {
    var w = $('#preview-canvas').width();
    var h = $('#preview-canvas').height();

    this.renderer.setSize($('#preview-canvas').width(), $('#preview-canvas').height());
    this.camera.aspect = w / h;

    if (this.aspectRatio() < 1) {
        this.camera.zoom = w / h;
    }

    /*
    var m = (w > h ? w : h);
    var t = (w > h ? h : w) / m;

    this.camera.fov = 60 / t;
    */

    this.camera.updateProjectionMatrix();
}
