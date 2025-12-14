#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <dlfcn.h>
#include "gui.h"
#include "process.h"

#define MAX_POLITIQUES 100

// Variables globales pour l'interface
typedef struct {
    Process *procs;
    int count;
    GtkWidget *window;
    GtkWidget *combo_politiques;
    GtkWidget *spin_quantum;
    GtkWidget *drawing_gantt;
    GtkWidget *text_journal;
    GtkWidget *text_stats;
    char politiques[MAX_POLITIQUES][200];
    int politique_count;
    int timeline[1000];
    int timeline_len;
    int start[MAXP];
    int end[MAXP];
} AppData;

// Charger les politiques disponibles
void charger_politiques_gui(AppData *app) {
    DIR *dir = opendir("politiques");
    if (!dir) {
        printf("Erreur : impossible d'ouvrir le dossier 'politiques'\n");
        return;
    }

    struct dirent *entry;
    app->politique_count = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        int len = strlen(entry->d_name);
        if (len < 3 || strcmp(entry->d_name + len - 2, ".c") != 0)
            continue;

        strcpy(app->politiques[app->politique_count], entry->d_name);
        app->politique_count++;
    }

    closedir(dir);
}

// Callback pour dessiner le diagramme de Gantt
static gboolean on_draw_gantt(GtkWidget *widget, cairo_t *cr, gpointer data) {
    AppData *app = (AppData *)data;
    
    if (app->timeline_len == 0) {
        return FALSE;
    }

    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);
    
    // Fond blanc
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    
    // Paramètres de dessin
    int margin_left = 80;
    int margin_top = 40;
    int row_height = 40;
    int max_time = app->timeline_len > 50 ? 50 : app->timeline_len;
    int cell_width = (width - margin_left - 20) / (max_time + 1);
    
    if (cell_width < 10) cell_width = 10;
    
    // Titre
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 14);
    cairo_move_to(cr, margin_left, 20);
    cairo_show_text(cr, "DIAGRAMME DE GANTT");
    
    // Dessiner l'échelle de temps
    cairo_set_font_size(cr, 10);
    for (int t = 0; t <= max_time; t++) {
        char buf[10];
        snprintf(buf, sizeof(buf), "%d", t);
        cairo_move_to(cr, margin_left + t * cell_width, margin_top - 5);
        cairo_show_text(cr, buf);
    }
    
    // Dessiner les processus
    cairo_set_font_size(cr, 12);
    
    // Couleurs prédéfinies pour chaque processus
    double colors[][3] = {
        {0.2, 0.4, 0.8},  // Bleu
        {0.8, 0.2, 0.2},  // Rouge
        {0.2, 0.8, 0.2},  // Vert
        {0.8, 0.6, 0.2},  // Orange
        {0.6, 0.2, 0.8},  // Violet
        {0.2, 0.8, 0.8},  // Cyan
        {0.8, 0.8, 0.2},  // Jaune
        {0.8, 0.4, 0.6}   // Rose
    };
    
    for (int i = 0; i < app->count; i++) {
        int y = margin_top + i * row_height + 10;
        
        // Nom du processus
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_move_to(cr, 10, y + 15);
        cairo_show_text(cr, app->procs[i].name);
        
        // Dessiner les blocs d'exécution
        double *color = colors[i % 8];
        cairo_set_source_rgb(cr, color[0], color[1], color[2]);
        
        for (int t = 0; t < max_time; t++) {
            if (app->timeline[t] == i) {
                int x = margin_left + t * cell_width;
                cairo_rectangle(cr, x, y, cell_width - 2, row_height - 10);
                cairo_fill(cr);
                
                // Bordure
                cairo_set_source_rgb(cr, 0, 0, 0);
                cairo_rectangle(cr, x, y, cell_width - 2, row_height - 10);
                cairo_set_line_width(cr, 1);
                cairo_stroke(cr);
                
                cairo_set_source_rgb(cr, color[0], color[1], color[2]);
            }
        }
    }
    
    return FALSE;
}

// Exécuter la politique sélectionnée
void executer_politique_gui(AppData *app, const char *politique_name, int quantum) {
    char path[300];
    char lib[300];
    
    snprintf(path, sizeof(path), "politiques/%s", politique_name);
    snprintf(lib, sizeof(lib), "politiques/%s.so", politique_name);
    
    remove(lib);
    
    // Compilation
    char cmd[800];
    snprintf(cmd, sizeof(cmd), "gcc -shared -fPIC -Isrc %s -o %s 2>/dev/null", path, lib);
    system(cmd);
    
    void *handle = dlopen(lib, RTLD_NOW);
    if (!handle) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    GTK_MESSAGE_ERROR,
                                                    GTK_BUTTONS_CLOSE,
                                                    "Erreur de chargement: %s", dlerror());
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    char func_name[200];
    strcpy(func_name, politique_name);
    func_name[strlen(func_name) - 2] = '\0';
    
    // Copier les processus pour ne pas modifier l'original
    Process procs_copy[MAXP];
    memcpy(procs_copy, app->procs, sizeof(Process) * app->count);
    
    // Capturer la sortie pour l'afficher dans l'interface
    FILE *old_stdout = stdout;
    FILE *temp_file = tmpfile();
    stdout = temp_file;
    
    // Exécuter la politique
    if (strcmp(func_name, "round_robin") == 0) {
        void (*rr_func)(Process*, int, int) = dlsym(handle, func_name);
        if (rr_func) {
            rr_func(procs_copy, app->count, quantum);
        }
    } else {
        void (*alg_func)(Process*, int) = dlsym(handle, func_name);
        if (alg_func) {
            alg_func(procs_copy, app->count);
        }
    }
    
    // Restaurer stdout et lire le fichier temporaire
    stdout = old_stdout;
    rewind(temp_file);
    
    char buffer[10000];
    size_t total_read = 0;
    size_t bytes_read;
    while ((bytes_read = fread(buffer + total_read, 1, sizeof(buffer) - total_read - 1, temp_file)) > 0) {
        total_read += bytes_read;
    }
    buffer[total_read] = '\0';
    fclose(temp_file);
    
    // Afficher dans le journal
    GtkTextBuffer *journal_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->text_journal));
    gtk_text_buffer_set_text(journal_buffer, buffer, -1);
    
    dlclose(handle);
    
    // Recalculer les données pour le Gantt
    // Trier selon l'arrivée
    for (int i = 0; i < app->count - 1; i++) {
        for (int j = i + 1; j < app->count; j++) {
            if (procs_copy[j].arrival < procs_copy[i].arrival) {
                Process tmp = procs_copy[i];
                procs_copy[i] = procs_copy[j];
                procs_copy[j] = tmp;
            }
        }
    }
    
    int time = 0;
    app->timeline_len = 0;
    
    for (int i = 0; i < app->count; i++) {
        if (time < procs_copy[i].arrival) {
            while (time < procs_copy[i].arrival) {
                app->timeline[app->timeline_len++] = -1;
                time++;
            }
        }
        
        app->start[i] = time;
        for (int d = 0; d < procs_copy[i].duration; d++) {
            app->timeline[app->timeline_len++] = i;
            time++;
        }
        app->end[i] = time;
    }
    
    // Redessiner le Gantt
    gtk_widget_queue_draw(app->drawing_gantt);
}

// Callback pour le bouton "Exécuter"
static void on_executer_clicked(GtkWidget *widget, gpointer data) {
    AppData *app = (AppData *)data;
    
    const char *politique_name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->combo_politiques));
    if (!politique_name) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    GTK_MESSAGE_WARNING,
                                                    GTK_BUTTONS_CLOSE,
                                                    "Veuillez sélectionner une politique");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    int quantum = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->spin_quantum));
    executer_politique_gui(app, politique_name, quantum);
}

// Lancer l'interface GTK -------------------------------------------------------------------------------------------------------------------
void lancer_interface_gtk(Process procs[], int count) {
    gtk_init(NULL, NULL);
    
    AppData *app = g_malloc(sizeof(AppData));
    app->procs = procs;
    app->count = count;
    app->timeline_len = 0;
    
    charger_politiques_gui(app);
    
    // Créer la fenêtre principale
    app->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->window), "Ordonnanceur de Processus");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 1200, 800);
    gtk_container_set_border_width(GTK_CONTAINER(app->window), 10);
    g_signal_connect(app->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    
    // Layout principal
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(app->window), vbox);
    
    // Zone de sélection de politique
    GtkWidget *hbox_controles = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(vbox), hbox_controles, FALSE, FALSE, 0);
    
    GtkWidget *label_politique = gtk_label_new("Politique:");
    gtk_box_pack_start(GTK_BOX(hbox_controles), label_politique, FALSE, FALSE, 0);
    
    app->combo_politiques = gtk_combo_box_text_new();
    for (int i = 0; i < app->politique_count; i++) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->combo_politiques), app->politiques[i]);
    }
    gtk_box_pack_start(GTK_BOX(hbox_controles), app->combo_politiques, FALSE, FALSE, 0);
    
    GtkWidget *label_quantum = gtk_label_new("Quantum:");
    gtk_box_pack_start(GTK_BOX(hbox_controles), label_quantum, FALSE, FALSE, 0);
    
    app->spin_quantum = gtk_spin_button_new_with_range(1, 100, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->spin_quantum), 2);
    gtk_box_pack_start(GTK_BOX(hbox_controles), app->spin_quantum, FALSE, FALSE, 0);
    
    GtkWidget *btn_executer = gtk_button_new_with_label("Exécuter");
    g_signal_connect(btn_executer, "clicked", G_CALLBACK(on_executer_clicked), app);
    gtk_box_pack_start(GTK_BOX(hbox_controles), btn_executer, FALSE, FALSE, 0);
    
    // Notebook pour les onglets
    GtkWidget *notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);
    
    // Onglet 1: Diagramme de Gantt
    GtkWidget *scroll_gantt = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_gantt),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    
    app->drawing_gantt = gtk_drawing_area_new();
    gtk_widget_set_size_request(app->drawing_gantt, 1000, 400);
    g_signal_connect(app->drawing_gantt, "draw", G_CALLBACK(on_draw_gantt), app);
    
    gtk_container_add(GTK_CONTAINER(scroll_gantt), app->drawing_gantt);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scroll_gantt,
                            gtk_label_new("Diagramme de Gantt"));
    
    // Onglet 2: Journal d'exécution
    GtkWidget *scroll_journal = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_journal),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    
    app->text_journal = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(app->text_journal), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(app->text_journal), TRUE);
    
    gtk_container_add(GTK_CONTAINER(scroll_journal), app->text_journal);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scroll_journal,
                            gtk_label_new("Journal d'exécution"));
    
    gtk_widget_show_all(app->window);
    gtk_main();
    
    g_free(app);
}
