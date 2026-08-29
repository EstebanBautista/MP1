# Usa la imagen oficial de Nginx
FROM nginx:alpine

# Elimina el contenido por defecto
RUN rm -rf /usr/share/nginx/html/*

# Copia los archivos de tu sitio
COPY . /usr/share/nginx/html

# Expone el puerto 80
EXPOSE 80

# Inicia Nginx
CMD ["nginx", "-g", "daemon off;"]