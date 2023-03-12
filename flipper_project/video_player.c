#include "video_player.h"
#include "video_player_hal.h"
#include "init_deinit.h"

#include <video_player_icons.h>
#include <furi.h>
#include <furi_hal.h>
#include <cli/cli.h>
#include <gui/gui.h>

bool open_file_stream(Stream* stream) 
{
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    bool result = false;
    FuriString* path;
    path = furi_string_alloc();
    furi_string_set(path, VIDEO_PLAYER_FOLDER);

    DialogsFileBrowserOptions browser_options;
    dialog_file_browser_set_basic_options(&browser_options, ".bnd", &I_vid_logo);
    browser_options.base_path = VIDEO_PLAYER_FOLDER;
    browser_options.hide_ext = false;

    bool ret = dialog_file_browser_show(dialogs, path, path, &browser_options);

    furi_record_close(RECORD_DIALOGS);
    if(ret) {
        if(!file_stream_open(stream, furi_string_get_cstr(path), FSAM_READ, FSOM_OPEN_EXISTING)) {
            FURI_LOG_E("FUCK! ", "Cannot open file \"%s\"", furi_string_get_cstr(path));
        } else {
            result = true;
        }
    }
    furi_string_free(path);
    return result;
}

void draw_callback(Canvas* canvas, void* ctx) {
    PlayerViewModel* model = (PlayerViewModel*)ctx;
    VideoPlayerApp* player = (VideoPlayerApp*)(model->player);

    canvas_draw_xbm(canvas, 0, 0, player->width, player->height, player->image_buffer);
}

bool input_callback(InputEvent* input_event, void* ctx) {
    // Проверяем, что контекст не нулевой
    furi_assert(ctx);
    PlayerView* player_view = (PlayerView*)ctx;
    VideoPlayerApp* player = (VideoPlayerApp*)(player_view->context);

    bool consumed = false;

    VideoPlayerEvent event = {
        .type = EventTypeInput, .input = *input_event};

    furi_message_queue_put(player->event_queue, &event, FuriWaitForever);

    consumed = true;
    return consumed;
}

int32_t video_player_app(void* p) {
    UNUSED(p);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool st = storage_simply_mkdir(storage, APPSDATA_FOLDER);
    st = storage_simply_mkdir(storage, VIDEO_PLAYER_FOLDER);
    UNUSED(st);
    furi_record_close(RECORD_STORAGE);

    VideoPlayerApp* player = init_player();

    if(open_file_stream(player->stream))
    {

    }

    else
    {
        player->quit = true;
        goto end;
    }

    char header[8];
    header[7] = '\0';
    stream_read(player->stream, (uint8_t*)header, 7);

    if(strcmp(header, "BND!VID") != 0)
    {
        player->quit = true;
        goto end;
    }

    stream_read(player->stream, (uint8_t*)&player->version, sizeof(player->version));
    stream_read(player->stream, (uint8_t*)&player->num_frames, sizeof(player->num_frames));
    stream_read(player->stream, (uint8_t*)&player->audio_chunk_size, sizeof(player->audio_chunk_size));
    stream_read(player->stream, (uint8_t*)&player->sample_rate, sizeof(player->sample_rate));
    stream_read(player->stream, &player->height, sizeof(player->height));
    stream_read(player->stream, &player->width, sizeof(player->width));

    player->buffer = (uint8_t*)malloc(player->audio_chunk_size * 2 + (uint32_t)player->height * (uint32_t)player->width / 8);
    memset(player->buffer, 0, player->audio_chunk_size * 2 + (uint32_t)player->height * (uint32_t)player->width / 8);

    player->image_buffer_length = (uint32_t)player->height * (uint32_t)player->width / 8;
    player->audio_buffer = (uint8_t*)&player->buffer[player->image_buffer_length];
    player->image_buffer = player->buffer;

    player_init_hardware_and_play(player);

    // Текущее событие типа кастомного типа VideoPlayerEvent
    VideoPlayerEvent event;

    view_dispatcher_switch_to_view(player->view_dispatcher, VIEW_PLAYER);

    end:;

    player->playing = true;

    // Бесконечный цикл обработки очереди событий
    while(!(player->quit)) {
        // Выбираем событие из очереди в переменную event (ждём бесконечно долго, если очередь пуста)
        // и проверяем, что у нас получилось это сделать
        furi_check(
            furi_message_queue_get(player->event_queue, &event, FuriWaitForever) == FuriStatusOk);

        // Наше событие — это нажатие кнопки
        if(event.type == EventTypeInput) {
            if(event.input.key == InputKeyBack)
            {
                player->quit = true;
            }

            if(event.input.key == InputKeyOk)
            {
                player->playing = !player->playing;
            }

            if(player->playing)
            {
                player_start();
            }

            else
            {
                player_stop();
            }
        }

        if(event.type == EventType1stHalf)
        {
            //uint8_t* audio_buffer = player->audio_buffer;
        
            //stream_read(player->stream, player->image_buffer, player->image_buffer_length);
            //stream_read(player->stream, audio_buffer, player->audio_chunk_size);

            stream_read(player->stream, player->image_buffer, player->image_buffer_length + player->audio_chunk_size);

            player->frames_played++;
        }

        if(event.type == EventType2ndHalf)
        {
            uint8_t* audio_buffer = &player->audio_buffer[player->audio_chunk_size];
        
            stream_read(player->stream, player->image_buffer, player->image_buffer_length);
            stream_read(player->stream, audio_buffer, player->audio_chunk_size);

            player->frames_played++;
        }

        if(player->frames_played == player->num_frames)
        {
            player->quit = true;
        }
    }

    deinit_player(player);
    player_deinit_hardware();

    return 0;
}