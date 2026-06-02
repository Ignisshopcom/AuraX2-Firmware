const config = {
    title: 'Ignis Studio',
    titleHTML: '<span>IGNIS</span> STUDIO',
    version: '3.0.0',
    versionHTML: '3.0',
    forcePlatform: 'none', // darwin
    project: {
        default_name: 'unnamed',
        default_leds: 80,
        max_leds: 180,
        min_leds: 50,
        history_limit: 64,
        post_default: 0,
        pixel_counts: {
            '50 pixels': { leds: 50, acc: true },
            '80 pixels': { leds: 80, acc: true },
            '130 pixels': { leds: 130, acc: true },
            '170 pixels': { leds: 170, acc: true },
        },
        pixel_count_custom: true,
        pixel_url: 'http://petr.holly.darkyork.com/ignis/pixel.json',
        node_calculate_frequency: false,
        node_frequency: 2500,
        max_line_frequency: 2500,
        debug_export: false,
    },
    audio: {
        envelope_width: 10000,
        envelope_height: 70,
        envelope_color: '#f4d142',
        envelope_color_alt: '#f29d26',
    },
    timeline: {
        default_duration: 15000,
        images_y: 10,
        images_height: 120,
        audio_height: 70,
        min_scale: 1,
        max_scale: 5000,
        scale_factor: 100,
    },
    rendering: {
        texture_quality: 512,
        thumbnail_quality: 300,
    },
    
    magick_darwin: './magick', 
    
        preview_instances: 5, // zvýšíme počet instancí o jednu
        previews: [
            { id: 'static', name: 'Static Preview' },
            { id: 'rotating', name: 'Rotating Preview' },
            { id: 'third', name: 'Third Preview' },
            { id: 'fourth', name: 'Fourth Preview' },
            { id: 'curved_static', name: 'Curved Static Preview' } // Nový náhled
        ],
        // další konfigurace...
    
    

}
