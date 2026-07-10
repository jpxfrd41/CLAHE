#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/* error values */
#define P3_OK (0)
#define P3_FAILED_TO_OPEN_FILE (-1)
#define P3_FAILED_TO_ALLOC (-2)
#define P3_INVALID_MAGIC (-3)
#define P3_INVALID_DIMENSIONS (-4)
#define P3_FAILED_TO_READ_FILE (-5)

#define P3_ERROR_TO_STR(E) (P3_ERROR_TO_STR_LUT[abs(E)])

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} P3Pixel;

typedef struct {
    P3Pixel* data;
    size_t width;
    size_t height;
} P3;

/* fwd decls */
int p3_make(P3* p3, size_t width, size_t height);
int p3_from(P3* p3, const char* path);
void p3_from__skip_whitespace(FILE* fp);
int p3_from__read_value(FILE* fp);
void p3_destroy(P3* p3);
void p3_greyscale(P3* p3);
void p3_clahe(P3* p3, int tile_size, int clip);
P3Pixel* p3_at(P3* p3, size_t x, size_t y);

static const char* P3_ERROR_TO_STR_LUT[] = {
    "P3_OK", // (0)
    "P3_FAILED_TO_OPEN_FILE", // (-1)
    "P3_FAILED_TO_ALLOC", // (-2)
    "P3_INVALID_MAGIC", // (-3)
    "P3_INVALID_DIMENSIONS", // (-4)
    "P3_FAILED_TO_READ_FILE", // (-5)
};

int p3_make(P3* p3, size_t width, size_t height) {
    p3->data = malloc(sizeof(P3Pixel) * width * height);
    if (p3->data == NULL)
        return P3_FAILED_TO_ALLOC;
    p3->width = width;
    p3->height = height;
    return P3_OK;
}

int p3_from(P3* p3, const char* path) {
    int status = P3_OK;
    
    FILE* fp = fopen(path, "r");
    if (fp == NULL)
        return P3_FAILED_TO_OPEN_FILE;
    
    /* magic */
    p3_from__skip_whitespace(fp);
    if (fgetc(fp) != 'P' || fgetc(fp) != '3') {
        status = P3_INVALID_MAGIC;
        goto cleanup;
    }
    
    /* width & Height */
    p3_from__skip_whitespace(fp);
    p3->width = p3_from__read_value(fp);
    p3_from__skip_whitespace(fp);
    p3->height = p3_from__read_value(fp);
    
    p3_make(p3, p3->width, p3->height);
    printf("%s - %ldx%ld\n", path, p3->width, p3->height);
    
    /* (skip) maximum colour value */
    p3_from__skip_whitespace(fp);
    (void)p3_from__read_value(fp);
    p3_from__skip_whitespace(fp);
    
    /* pixels */
    for (size_t y = 0; y < p3->height; y++) {
        for (size_t x = 0; x < p3->width; x++) {
            p3_from__skip_whitespace(fp);
            p3->data[y * p3->width + x].red = (uint8_t)p3_from__read_value(fp);
            p3_from__skip_whitespace(fp);
            p3->data[y * p3->width + x].green = (uint8_t)p3_from__read_value(fp);
            p3_from__skip_whitespace(fp);
            p3->data[y * p3->width + x].blue= (uint8_t)p3_from__read_value(fp);
        }
    }       

cleanup:
    fclose(fp);
    return status;
}

void p3_from__skip_whitespace(FILE* fp) {
    char c = 0;
    while ((c = (char)fgetc(fp)) != EOF && isspace(c));
    /* Rewind back one */
    if (c != EOF)
        ungetc(c, fp);
}

int p3_from__read_value(FILE* fp) {
    char buffer[64] = {0};
    size_t i = 0;
    char c = 0;
    while ((c = (char)fgetc(fp)) != EOF && !isspace(c))
        buffer[i++] = c;
    buffer[i] = '\0';
    return atoi(buffer);
}

void p3_destroy(P3* p3) {
    if (p3->data != NULL) {
        free(p3->data);
        p3->data = NULL;
    }
    p3->width = 0;
    p3->height = 0;
}

void p3_greyscale(P3* p3) {
    for (size_t y = 0; y < p3->height; y++) {
        for (size_t x = 0; x < p3->width; x++) {
            P3Pixel* pixel = p3_at(p3, x, y);
            P3Pixel pixel_copy = *pixel;
            uint8_t grey = (uint8_t)((pixel_copy.red + pixel_copy.green + pixel_copy.blue) / 3);
            pixel->red = grey;
            pixel->green = grey;
            pixel->blue = grey;
        }
    }    
}

void p3_clahe(P3* p3, int tile_size, int clip) {
    size_t width = p3->width;
    size_t height = p3->height;

    size_t tiles_x_count = (width + tile_size - 1) / tile_size;
    size_t tiles_y_count = (height + tile_size - 1) / tile_size;

    for (size_t ty = 0; ty < tiles_y_count; ty++) {
        size_t y0 = ty * tile_size;
        size_t y1 = y0 + tile_size;
        if (y1 > height) 
            y1 = height;

        for (size_t tx = 0; tx < tiles_x_count; tx++) {
            size_t x0 = tx * tile_size;
            size_t x1 = x0 + tile_size;
            if (x1 > width) 
                x1 = width;

            int hist[256] = {0};
            size_t tile_pixels = 0;

            /* build histogram for this tile */
            for (size_t y = y0; y < y1; y++) {
                for (size_t x = x0; x < x1; x++) {
                    P3Pixel* pixel = p3_at(p3, x, y);
                    hist[pixel->red]++;
                    tile_pixels++;
                }
            }

            if (tile_pixels == 0)
                continue;

            /* simple clipping and uniform redistribution of excess */
            if (clip > 0) {
                int excess = 0;
                for (int i = 0; i < 256; i++) {
                    if (hist[i] > clip) {
                        excess += hist[i] - clip;
                        hist[i] = clip;
                    }
                }
                if (excess > 0) {
                    int add = excess / 256;
                    int rem = excess % 256;
                    for (int i = 0; i < 256; i++) 
                        hist[i] += add;
                    for (int i = 0; i < rem; i++) 
                        hist[i]++;
                }
            }

            /* build LUT from cumulative distribution */
            uint32_t cdf = 0;
            uint8_t lut[256];
            for (int i = 0; i < 256; i++) {
                cdf += hist[i];
                lut[i] = (uint8_t)((cdf * 255) / (uint32_t)tile_pixels);
            }

            /* map pixels in this tile using the LUT (in-place) */
            for (size_t y = y0; y < y1; y++) {
                for (size_t x = x0; x < x1; x++) {
                    P3Pixel* pixel = p3_at(p3, x, y);
                    uint8_t out = lut[pixel->red];
                    pixel->red = out;
                    pixel->green = out;
                    pixel->blue = out;
                }
            }
        }
    }
}

P3Pixel* p3_at(P3* p3, size_t x, size_t y) {
    return p3->data + (y * p3->width) + x;
}


/*=============================================================================
 * Win32
 *=============================================================================*/
static P3 g_p3 = {0};
static int g_clip_limit = 10;
static int g_tile_size = 1;
static int g_tile_size_px = 2;

LRESULT CALLBACK window_processor(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint_struct = {0};
        HDC device_ctx = BeginPaint(window, &paint_struct);
        for (size_t y = 0; y < g_p3.height; y++) {
            for (size_t x = 0; x < g_p3.width; x++) {
                COLORREF colour = RGB(
                    g_p3.data[y * g_p3.width + x].red,
                    g_p3.data[y * g_p3.width + x].green,
                    g_p3.data[y * g_p3.width + x].blue
                );
                SetPixel(device_ctx, x, y, colour);
            }
        }
        EndPaint(window, &paint_struct);
        return 0;
    }
    case WM_KEYDOWN: 
    case WM_SYSKEYDOWN: {
        switch (lparam) {
        /* tile size (2^) */
        case VK_UP: g_tile_size++; g_tile_size_px = (int)pow(2.0f, g_tile_size); break;
        case VK_DOWN: g_tile_size--; = g_tile_size_px = (int)pow(2.0f, g_tile_size); break;
        /* clip limit */
        case VK_LEFT: g_clip_limit--; break;
        case VK_RIGHT: g_clip_limit++; break;
        }
        printf("clip_limit: %d\ntile_size: %d\n", g_clip_limit, g_tile_size_px);
        return 0; 
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE prev_instance, LPSTR cmd_line, int cmd_show) {
    if (AllocConsole()) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        freopen("CONIN$", "r", stdin);
    }
    
    int p3_status = p3_from(&g_p3, "w-p3.ppm");
    if (p3_status != P3_OK) {
        fprintf(stderr, "error: Failed to load the image - %s\n", P3_ERROR_TO_STR(p3_status));
        getchar();
        FreeConsole();
        return 1;
    }
    p3_greyscale(&g_p3);
    p3_clahe(&g_p3, 8, 5);
    
    WNDCLASS window_class = {0};
    window_class.style = CS_VREDRAW | CS_HREDRAW;
    window_class.lpszClassName = "CLAHE";
    window_class.lpfnWndProc = window_processor;
    window_class.hInstance = instance;
    RegisterClassA(&window_class);

    HWND window = CreateWindowA(
        "CLAHE", 
        "CLAHE",  
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 
        CW_USEDEFAULT, 
        CW_USEDEFAULT, 
        g_p3.width, 
        g_p3.height, 
        NULL, 
        NULL, 
        instance, 
        NULL
    );
    if (window == NULL) {
        fprintf(stderr, "error: Failed to create the window\n");
        FreeConsole();
        return 1;
    }

    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    MSG message = {0};
    while (GetMessage(&message, NULL, 0, 0) != 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }

    p3_destroy(&g_p3);
    FreeConsole();
    return 0;
}
